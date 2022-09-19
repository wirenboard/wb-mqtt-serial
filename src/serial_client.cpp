#include "serial_client.h"
#include <chrono>
#include <iostream>
#include <unistd.h>

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
        if (dev) {
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
        return false;
    }

    bool IsHighPriority(const TRegister& reg)
    {
        return bool(reg.ReadPeriod);
    }

    Metrics::TPollItem ToMetricsPollItem(const std::string& device, const std::list<PRegister>& regs)
    {
        Metrics::TPollItem res;
        res.Device = device;
        for (const auto& reg: regs) {
            res.Controls.push_back(reg->GetChannelName());
        }
        return res;
    }

    class TRegisterReader
    {
        PRegisterRange RegisterRange;
        std::chrono::milliseconds MaxPollTime;
        PSerialDevice Device;

    public:
        TRegisterReader(std::chrono::milliseconds maxPollTime): MaxPollTime(maxPollTime)
        {}

        bool operator()(const PRegister& reg, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
        {
            if (!Device) {
                RegisterRange = reg->Device()->CreateRegisterRange();
                Device = reg->Device();
            }
            if (Device != reg->Device()) {
                return false;
            }
            if (policy == TItemAccumulationPolicy::Force) {
                return RegisterRange->Add(reg, std::chrono::milliseconds::max());
            }
            return RegisterRange->Add(reg, std::min(MaxPollTime, pollLimit));
        }

        PRegisterRange GetRegisterRange() const
        {
            return RegisterRange;
        }
    };

    class TClosedPortRegisterReader
    {
        std::list<PRegister> Regs;

    public:
        bool operator()(const PRegister& reg, TItemAccumulationPolicy policy, std::chrono::milliseconds pollLimit)
        {
            Regs.emplace_back(reg);
            return true;
        }

        std::list<PRegister>& GetRegisters()
        {
            return Regs;
        }
    };
};

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices,
                             PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             Metrics::TMetrics& metrics,
                             size_t lowPriorityRateLimit)
    : Port(port),
      Devices(devices),
      Active(false),
      Scheduler(MAX_LOW_PRIORITY_LAG, lowPriorityRateLimit),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      Metrics(metrics),
      ThrottlingStateLogger()
{
    FlushNeeded = std::make_shared<TBinarySemaphore>();
    RPCRequestHandler = std::make_shared<TRPCRequestHandler>();
    RegisterUpdateSignal = FlushNeeded->MakeSignal();
    RPCSignal = FlushNeeded->MakeSignal();
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
        if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
            Scheduler.AddEntry(reg, now, IsHighPriority(*reg) ? TPriority::High : TPriority::Low);
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

void TSerialClient::WaitForPollAndFlush(std::chrono::steady_clock::time_point waitUntil)
{
    auto now = std::chrono::steady_clock::now();
    if (now >= waitUntil) {
        MaybeFlushAvoidingPollStarvationButDontWait();
        return;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription()
                   << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
                   << ": Wait until "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil.time_since_epoch()).count();
    }

    // Limit waiting time ro be responsive
    waitUntil = std::min(waitUntil, now + MAX_POLL_TIME);
    while (FlushNeeded->Wait(waitUntil)) {
        if (FlushNeeded->GetSignalValue(RegisterUpdateSignal)) {
            DoFlush();
            Metrics.StartPoll(Metrics::BUS_IDLE);
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            // End session with current device to make bus clean for RPC
            PrepareToAccessDevice(LastAccessedDevice, NULL, Metrics);
            LastAccessedDevice = NULL;
            RPCRequestHandler->RPCRequestHandling(Port);
        }
    }
}

void TSerialClient::UpdateFlushNeeded()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (handler->NeedToFlush()) {
            FlushNeeded->Signal(RegisterUpdateSignal);
            break;
        }
    }
}

void TSerialClient::MaybeFlushAvoidingPollStarvationButDontWait()
{
    // avoid poll starvation
    int flush_count_remaining = MAX_FLUSHES_WHEN_POLL_IS_DUE;
    bool continueSignalHandling;

    do {
        continueSignalHandling = false;
        if (FlushNeeded->GetSignalValue(RegisterUpdateSignal)) {
            DoFlush();
            continueSignalHandling = true;
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            // End session with current device to make bus clean for RPC
            PrepareToAccessDevice(LastAccessedDevice, NULL, Metrics);
            LastAccessedDevice = NULL;
            RPCRequestHandler->RPCRequestHandling(Port);
            continueSignalHandling = true;
        }

        flush_count_remaining--;
    } while (continueSignalHandling && flush_count_remaining > 0);
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
    if (reg->GetErrorState().test(TRegister::ReadError) || reg->GetErrorState().test(TRegister::WriteError)) {
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
    Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
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
    Scheduler.AddEntry(reg, pollStartTime + 1us, TPriority::Low);
}

void TSerialClient::ClosedPortCycle()
{
    auto now = std::chrono::steady_clock::now();
    auto wait_until = Scheduler.GetDeadline(now);
    if (wait_until - now > MAX_CLOSED_PORT_CYCLE_TIME) {
        wait_until = now + MAX_CLOSED_PORT_CYCLE_TIME;
    }

    while (FlushNeeded->Wait(wait_until)) {
        if (FlushNeeded->GetSignalValue(RegisterUpdateSignal)) {
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
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            RPCRequestHandler->RPCRequestHandling(Port);
        }
    }

    TClosedPortRegisterReader reader;
    Scheduler.AccumulateNext(wait_until, reader);
    for (auto& reg: reader.GetRegisters()) {
        SetReadError(reg);
        ScheduleNextPoll(reg, wait_until);
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
    FlushNeeded->Signal(RegisterUpdateSignal);
}

void TSerialClient::SetReadCallback(const TSerialClient::TCallback& callback)
{
    ReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TCallback& callback)
{
    ErrorCallback = callback;
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

void TSerialClient::OpenPortCycle()
{
    WaitForPollAndFlush(Scheduler.GetDeadline(std::chrono::steady_clock::now()));
    auto pollStartTime = std::chrono::steady_clock::now();
    Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);

    TRegisterReader reader(MAX_POLL_TIME);

    auto throttlingState = Scheduler.AccumulateNext(pollStartTime, reader);
    auto throttlingMsg = ThrottlingStateLogger.GetMessage(throttlingState);
    if (!throttlingMsg.empty()) {
        LOG(Warn) << Port->GetDescription() << " " << throttlingMsg;
    }
    auto range = reader.GetRegisterRange();
    if (!range) {
        // Nothing to read
        return;
    }
    if (range->RegisterList().empty()) {
        // There are registers waiting read but they don't fit in allowed poll limit
        // Wait for high priority registers
        WaitForPollAndFlush(Scheduler.GetHighPriorityDeadline());
        return;
    }

    auto device = range->RegisterList().front()->Device();
    bool deviceWasConnected = !device->GetIsDisconnected();
    if (PrepareToAccessDevice(LastAccessedDevice, device, Metrics)) {
        LastAccessedDevice = device;
        Metrics.StartPoll(ToMetricsPollItem(device->DeviceConfig()->Id, range->RegisterList()));
        device->ReadRegisterRange(range);
        Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(pollStartTime);
            ProcessPolledRegister(reg);
            ScheduleNextPoll(reg, pollStartTime);
        }
    } else {
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(pollStartTime);
            ScheduleNextPoll(reg, pollStartTime);
            SetReadError(reg);
        }
    }
    if (deviceWasConnected && device->GetIsDisconnected()) {
        SetRegistersAvailability(device, TRegisterAvailability::UNKNOWN);
    }
    OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    UpdateFlushNeeded();
    Metrics.StartPoll(Metrics::BUS_IDLE);
}

PPort TSerialClient::GetPort()
{
    return Port;
}

std::vector<uint8_t> TSerialClient::RPCTransceive(PRPCRequest request) const
{
    return RPCRequestHandler->RPCTransceive(request, FlushNeeded, RPCSignal);
}

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Device() != r2->Device()) {
        return r1->Device()->DeviceConfig()->SlaveId > r2->Device()->DeviceConfig()->SlaveId;
    }
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    auto cmp = r1->GetAddress().Compare(r2->GetAddress());
    if (cmp < 0) {
        return false;
    }
    if (cmp > 0) {
        return true;
    }
    // addresses are equal, compare offsets
    return r1->GetDataOffset() > r2->GetDataOffset();
}

TThrottlingStateLogger::TThrottlingStateLogger(): FirstTime(true)
{}

std::string TThrottlingStateLogger::GetMessage(TThrottlingState state)
{
    if (FirstTime && state == TThrottlingState::LowPriorityRateLimit) {
        FirstTime = false;
        return "Register read rate limit is exceeded";
    }
    return std::string();
}
