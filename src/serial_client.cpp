#include "serial_client.h"

#include <chrono>
#include <iostream>
#include <unistd.h>

#include "rpc_port_handler.h"
#include <wblib/json_utils.h>

using namespace std::chrono_literals;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto PORT_OPEN_ERROR_NOTIFICATION_INTERVAL = 5min;
    const auto MAX_CLOSED_PORT_CYCLE_TIME = 500ms;
    const auto MAX_POLL_TIME = 100ms;
    const auto MAX_LOW_PRIORITY_LAG = 1s;
    const auto MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;

    bool PrepareToAccessDevice(PSerialDevice lastAccessedDevice, PSerialDevice dev, Metrics::TMetrics& metrics)
    {
        if (lastAccessedDevice && dev != lastAccessedDevice) {
            try {
                metrics.StartPoll({lastAccessedDevice->DeviceConfig()->Id, "End session"});
                lastAccessedDevice->EndSession();
            } catch (const TSerialDeviceException& e) {
                auto& logger = lastAccessedDevice->GetIsDisconnected() ? Debug : Warn;
                LOG(logger) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is "
                            << lastAccessedDevice->ToString() + "]";
            }
        }
        try {
            metrics.StartPoll({dev->DeviceConfig()->Id, "Start session"});
            bool deviceWasDisconnected = dev->GetIsDisconnected();
            if (deviceWasDisconnected || dev != lastAccessedDevice) {
                dev->Prepare();
            }
            return true;
        } catch (const TSerialDeviceException& e) {
            auto& logger = dev->GetIsDisconnected() ? Debug : Warn;
            LOG(logger) << "Failed to open session: " << e.what() << " [slave_id is " << dev->ToString() + "]";
            return false;
        }
    }

    bool IsHighPriority(const TRegister& reg)
    {
        return bool(reg.ReadPeriod);
    }
};

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices,
                             PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             Metrics::TMetrics& metrics)
    : Port(port),
      Devices(devices),
      Active(false),
      FlushNeeded(new TBinarySemaphore),
      Scheduler(TPreemptivePolicy(MAX_LOW_PRIORITY_LAG)),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      Metrics(metrics)
{
    RegisterSignal = FlushNeeded->SignalRegistration();
    RPCSignal = FlushNeeded->SignalRegistration();
}

TSerialClient::~TSerialClient()
{
    Active = false;
    if (Port->IsOpen()) {
        Port->Close();
    }

    // remove all registered devices
    ClearDevices();
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg);
    RegList.push_back(reg);
    LOG(Debug) << "AddRegister: " << reg;
}

void TSerialClient::Activate()
{
    if (!Active) {
        if (Handlers.empty())
            throw TSerialDeviceException(Port->GetDescription() + " no registers defined");
        Active = true;
        PrepareRegisterRanges();
    }
}

void TSerialClient::Connect()
{
    OpenCloseLogic.OpenIfAllowed(Port);
}

void TSerialClient::PrepareRegisterRanges()
{
    auto now = std::chrono::steady_clock::now();
    for (auto& reg: RegList) {
        if (!reg->WriteOnly) {
            // All registers are marked as high priority with poll time set to now.
            // So they will be polled as soon as possible after service start.
            // During next polls registers will be divided to low or high priority according to poll interval
            Scheduler.AddEntry(reg, now, TPriority::High);
        }
    }
}

void TSerialClient::DoFlush()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        if (PrepareToAccessDevice(LastAccessedDevice, handler->Device(), Metrics)) {
            LastAccessedDevice = handler->Device();
            Metrics.StartPoll({handler->Device()->DeviceConfig()->Id, "Command"});
            handler->Flush();
        } else {
            reg->SetError(TRegister::TError::WriteError);
        }
        Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
        if (reg->GetErrorState().test(TRegister::TError::WriteError)) {
            if (ErrorCallback) {
                ErrorCallback(reg);
            }
        } else {
            if (ReadCallback) {
                ReadCallback(reg);
            }
        }
    }
}

void TSerialClient::WaitForPollAndFlush()
{
    auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point wait_until = Scheduler.GetNextPollTime();
    if (now >= wait_until) {
        MaybeFlushAvoidingPollStarvationButDontWait();
        return;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription()
                   << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                   << ": Wait until "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(wait_until.time_since_epoch()).count();
    }

    while (FlushNeeded->Wait(wait_until)) {
        if (FlushNeeded->GetSignalValue(RegisterSignal)) {
            DoFlush();
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            RPCPortHandler.RPCRequestHandling(Port);
        }

        FlushNeeded->ResetAllSignals();
    }
}

void TSerialClient::UpdateFlushNeeded()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (handler->NeedToFlush()) {
            FlushNeeded->Signal(RegisterSignal);
            break;
        }
    }
}

void TSerialClient::MaybeFlushAvoidingPollStarvationButDontWait()
{
    // avoid poll starvation
    int flush_count_remaining = MAX_FLUSHES_WHEN_POLL_IS_DUE;
    while (flush_count_remaining--) {
        if (FlushNeeded->GetSignalValue(RegisterSignal)) {
            DoFlush();
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            RPCPortHandler.RPCRequestHandling(Port);
        }
    }
}

void TSerialClient::SetReadError(PRegister reg)
{
    reg->SetError(TRegister::TError::ReadError);
    if (ErrorCallback) {
        ErrorCallback(reg);
    }
}

void TSerialClient::ProcessPolledRegister(PRegister reg)
{
    if (reg->GetErrorState().count()) {
        if (ErrorCallback) {
            ErrorCallback(reg);
        }
    } else {
        if (ReadCallback) {
            ReadCallback(reg);
        }
    }
}

void TSerialClient::Cycle()
{
    Activate();

    try {
        OpenCloseLogic.OpenIfAllowed(Port);
    } catch (const std::exception& e) {
        ConnectLogger.Log(e.what(), Debug, Error);
    }

    if (Port->IsOpen()) {
        ConnectLogger.DropTimeout();
        OpenPortCycle();
    } else {
        ClosedPortCycle();
    }
}

void TSerialClient::ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime)
{
    if (reg->GetAvailable() == TRegisterAvailability::UNAVAILABLE) {
        return;
    }
    if (IsHighPriority(*reg)) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadPeriod), TPriority::High);
        return;
    }
    if (reg->ReadRateLimit) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadRateLimit), TPriority::Low);
        return;
    }
    // Low priority tasks should be scheduled to read as soon as possible,
    // but with a small delay after current read.
    Scheduler.AddEntry(reg, pollStartTime + 1ms, TPriority::Low);
}

void TSerialClient::ClosedPortCycle()
{
    auto now = std::chrono::steady_clock::now();
    auto wait_until = Scheduler.GetNextPollTime();
    if (wait_until - now > MAX_CLOSED_PORT_CYCLE_TIME) {
        wait_until = now + MAX_CLOSED_PORT_CYCLE_TIME;
    }
    while (FlushNeeded->Wait(wait_until)) {
        if (FlushNeeded->GetSignalValue(RegisterSignal)) {

            for (const auto& reg: RegList) {
                auto handler = Handlers[reg];
                if (!handler->NeedToFlush())
                    continue;
                reg->SetError(TRegister::TError::WriteError);
                if (ErrorCallback) {
                    ErrorCallback(reg);
                }
            }
        }
        FlushNeeded->ResetAllSignals();
    }

    now = std::chrono::steady_clock::now();
    TClosedPortRegisterReader reader;
    Scheduler.AccumulateNext(now, reader);
    for (auto& reg: reader.GetRegisters()) {
        SetReadError(reg);
        ScheduleNextPoll(reg, now);
        reg->Device()->SetTransferResult(false);
    }
}

void TSerialClient::ClearDevices()
{
    Devices.clear();
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
    FlushNeeded->Signal(RegisterSignal);
}

void TSerialClient::SetReadCallback(const TSerialClient::TCallback& callback)
{
    ReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::NotifyFlushNeeded()
{
    FlushNeeded->Signal(RegisterSignal);
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
}

void TSerialClient::SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability)
{
    for (auto& reg: RegList) {
        if (reg->Device() == dev) {
            reg->SetAvailable(availability);
        }
    }
}

std::vector<PRegisterRange> TSerialClient::ReadRanges(TRegisterReader& reader,
                                                      PRegisterRange range,
                                                      std::chrono::steady_clock::time_point pollStartTime,
                                                      bool forceError)
{
    std::vector<PRegisterRange> ranges;
    bool firstIteration = true;
    auto device = range->Device();
    while (true) {
        ranges.push_back(range);
        // Must read first range to update device connection state
        if (!forceError && (firstIteration || !device->GetIsDisconnected())) {
            firstIteration = false;
            device->ReadRegisterRange(range);
            Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
            for (auto& reg: range->RegisterList()) {
                ProcessPolledRegister(reg);
            }
        } else {
            for (auto& reg: range->RegisterList()) {
                SetReadError(reg);
            }
        }
        auto now = std::chrono::steady_clock::now();
        auto delta = MAX_POLL_TIME - std::chrono::duration_cast<std::chrono::milliseconds>(now - pollStartTime);
        if (delta <= std::chrono::milliseconds::zero()) {
            break;
        }
        reader.Reset(delta);
        Scheduler.AccumulateNext(now, reader);
        range = reader.GetRegisterRange();
        if (!range) {
            break;
        }
    }
    return ranges;
}

void TSerialClient::OpenPortCycle()
{
    Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
    WaitForPollAndFlush();

    auto pollStartTime = std::chrono::steady_clock::now();
    TRegisterReader reader(MAX_POLL_TIME);

    Scheduler.AccumulateNext(pollStartTime, reader);
    auto range = reader.GetRegisterRange();
    if (!range) {
        return;
    }
    auto device = range->Device();
    bool deviceWasConnected = !device->GetIsDisconnected();
    bool forceError = true;
    if (PrepareToAccessDevice(LastAccessedDevice, device, Metrics)) {
        LastAccessedDevice = device;
        forceError = false;
    }
    auto ranges = ReadRanges(reader, range, pollStartTime, forceError);
    device->EndPollCycle();
    for (const auto& range: ranges) {
        for (const auto& r: range->RegisterList()) {
            ScheduleNextPoll(r, pollStartTime);
        }
    }
    if (deviceWasConnected && device->GetIsDisconnected()) {
        SetRegistersAvailability(device, TRegisterAvailability::UNKNOWN);
    }
    OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    UpdateFlushNeeded();
    Metrics.StartPoll(Metrics::BUS_IDLE);
}

bool TSerialClient::RPCTransceive(const TRPCPortConfig& config,
                                  std::vector<uint8_t>& response,
                                  size_t& actualResponseSize)
{
    return RPCPortHandler.RPCTransceive(config, response, actualResponseSize, FlushNeeded, RPCSignal);
}

Json::Value TSerialClient::GetPortPath()
{
    return Port->GetPath();
}

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Device() != r2->Device()) {
        return r1->Device()->DeviceConfig()->SlaveId > r2->Device()->DeviceConfig()->SlaveId;
    }
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    if (r1->GetAddress() < r2->GetAddress()) {
        return false;
    }
    if (r2->GetAddress() < r1->GetAddress()) {
        return true;
    }
    // addresses are equal, compare offsets
    return r1->BitOffset > r2->BitOffset;
}

TRegisterReader::TRegisterReader(std::chrono::milliseconds maxPollTime): MaxPollTime(maxPollTime)
{}

bool TRegisterReader::operator()(const PRegister& reg, std::chrono::milliseconds pollLimit)
{
    if (!RegisterRange) {
        if (Device) {
            if (Device != reg->Device()) {
                return false;
            }
        } else {
            Device = reg->Device();
        }
        RegisterRange = Device->CreateRegisterRange(reg);
        return true;
    }
    return RegisterRange->Add(reg, std::min(MaxPollTime, pollLimit));
}

PRegisterRange TRegisterReader::GetRegisterRange() const
{
    return RegisterRange;
}

bool TClosedPortRegisterReader::operator()(const PRegister& reg, std::chrono::milliseconds pollLimit)
{
    Regs.emplace_back(reg);
    return true;
}

std::list<PRegister>& TClosedPortRegisterReader::GetRegisters()
{
    return Regs;
}

void TRegisterReader::Reset(std::chrono::milliseconds maxPollTime)
{
    MaxPollTime = maxPollTime;
    RegisterRange.reset();
}
