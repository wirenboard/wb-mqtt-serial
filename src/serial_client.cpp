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
        try {
            metrics.StartPoll({dev->DeviceConfig()->Id, "Start session"});
            bool deviceWasDisconnected = dev->GetIsDisconnected();
            if (deviceWasDisconnected || dev != lastAccessedDevice) {
                dev->Prepare();
            }
            return true;
        } catch (const TSerialDeviceException& e) {
            // TODO: Show error if it is a first poll
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
      ReadCallback([](PRegister, bool) {}),
      ErrorCallback([](PRegister, bool) {}),
      FlushNeeded(new TBinarySemaphore),
      Scheduler(MAX_POLL_TIME, TPreemptivePolicy(MAX_LOW_PRIORITY_LAG)),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      Metrics(metrics)
{}

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
    auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg, FlushNeeded);
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

void TSerialClient::MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state)
{
    if (state != TRegisterHandler::UnknownErrorState && state != TRegisterHandler::ErrorStateUnchanged)
        ErrorCallback(reg, state);
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
            auto flushRes = handler->Flush();
            Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
            if (handler->CurrentErrorState() != TRegisterHandler::WriteError &&
                handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
            {
                ReadCallback(reg, flushRes.ValueIsChanged);
            }
            MaybeUpdateErrorState(reg, flushRes.Error);
        } else {
            MaybeUpdateErrorState(reg, handler->Flush(TRegisterHandler::WriteError).Error);
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
        DoFlush();
    }
}

void TSerialClient::UpdateFlushNeeded()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (handler->NeedToFlush()) {
            FlushNeeded->Signal();
            break;
        }
    }
}

void TSerialClient::MaybeFlushAvoidingPollStarvationButDontWait()
{
    // avoid poll starvation
    int flush_count_remaining = MAX_FLUSHES_WHEN_POLL_IS_DUE;
    while (flush_count_remaining-- && FlushNeeded->TryWait())
        DoFlush();
}

void TSerialClient::SetReadError(PRegister reg)
{
    reg->SetError(ST_UNKNOWN_ERROR);
    bool changed;
    auto handler = Handlers[reg];
    // TBD: separate AcceptDeviceReadError method (changed is unused here)
    MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
}

void TSerialClient::ProcessPolledRegister(PRegister reg)
{
    auto handler = Handlers[reg];
    bool changed;
    if (reg->GetError()) {
        // TBD: separate AcceptDeviceReadError method (changed is unused here)
        MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
        return;
    }
    MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(reg->GetValue(), true, &changed));
    // Note that handler->CurrentErrorState() is not the
    // same as the value returned by handler->AcceptDeviceValue(...),
    // because the latter may be ErrorStateUnchanged.
    if (handler->CurrentErrorState() != TRegisterHandler::ReadError &&
        handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
    {
        ReadCallback(reg, changed);
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

void TSerialClient::ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point now)
{
    if (reg->GetAvailable() == TRegisterAvailability::UNAVAILABLE) {
        return;
    }
    if (IsHighPriority(*reg)) {
        Scheduler.AddEntry(reg, now + *(reg->ReadPeriod), TPriority::High);
        return;
    }
    if (reg->ReadRateLimit) {
        Scheduler.AddEntry(reg, now + *(reg->ReadRateLimit), TPriority::Low);
        return;
    }
    Scheduler.AddEntry(reg, now, TPriority::Low);
}

void TSerialClient::ClosedPortCycle()
{
    auto now = std::chrono::steady_clock::now();
    auto wait_until = Scheduler.GetNextPollTime();
    if (wait_until - now > MAX_CLOSED_PORT_CYCLE_TIME) {
        wait_until = now + MAX_CLOSED_PORT_CYCLE_TIME;
    }
    while (FlushNeeded->Wait(wait_until)) {
        for (const auto& reg: RegList) {
            auto handler = Handlers[reg];
            if (!handler->NeedToFlush())
                continue;
            MaybeUpdateErrorState(reg, handler->Flush(TRegisterHandler::WriteError).Error);
        }
    }
    now = std::chrono::steady_clock::now();
    TClosedPortRegisterReader reader;
    Scheduler.AccumulateNext(std::chrono::steady_clock::now(), reader);
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
}

std::string TSerialClient::GetTextValue(PRegister reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TSerialClient::DidRead(PRegister reg) const
{
    return GetHandler(reg)->DidRead();
}

void TSerialClient::SetReadCallback(const TSerialClient::TReadCallback& callback)
{
    ReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TErrorCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::NotifyFlushNeeded()
{
    FlushNeeded->Signal();
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
    Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);
    WaitForPollAndFlush();

    auto now = std::chrono::steady_clock::now();
    TRegisterReader reader(now, LastAccessedDevice, Metrics);
    Scheduler.AccumulateNext(now, reader);
    if (reader.GetRegisters().empty()) {
        return;
    }
    reader.Read();

    Metrics.StartPoll(Metrics::NON_BUS_POLLING_TASKS);

    auto device = reader.GetRegisters().back()->Device();
    LastAccessedDevice = device;
    device->EndPollCycle();

    for (auto& reg: reader.GetRegisters()) {
        ProcessPolledRegister(reg);
        ScheduleNextPoll(reg, now);
    }

    UpdateFlushNeeded();

    if (!reader.GetDeviceWasDisconnected() && device->GetIsDisconnected()) {
        SetRegistersAvailability(device, TRegisterAvailability::UNKNOWN);
    }

    OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    Metrics.StartPoll(Metrics::BUS_IDLE);
}

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Device() != r2->Device()) {
        return r1->Device()->DeviceConfig()->SlaveId > r2->Device()->DeviceConfig()->SlaveId;
    }
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    return !(r1->GetAddress() < r2->GetAddress());
}

TRegisterReader::TRegisterReader(std::chrono::steady_clock::time_point pollStart,
                                 PSerialDevice lastAccessedDevice,
                                 Metrics::TMetrics& metrics)
    : PollStart(pollStart),
      Metrics(metrics),
      LastAccessedDevice(lastAccessedDevice)
{}

bool TRegisterReader::operator()(const PRegister& reg, std::chrono::milliseconds pollLimit)
{
    if (Regs.empty()) {
        DeviceWasDisconnected = reg->Device()->GetIsDisconnected();
        Regs.emplace_back(reg);
        if (PrepareToAccessDevice(LastAccessedDevice, reg->Device(), Metrics)) {
            RegisterRange = reg->Device()->CreateRegisterRange(reg);
            if (!RegisterRange) {
                // Reading range is not supported read one by one
                reg->Device()->ReadRegister(reg);
                if (reg->GetError() != ST_UNKNOWN_ERROR) {
                    DeviceIsConnected = true;
                }
            }
        } else {
            reg->SetError(ST_UNKNOWN_ERROR);
        }
        return true;
    }

    if (Regs.back()->Device() != reg->Device()) {
        return false;
    }

    if (RegisterRange) {
        if (RegisterRange->Add(reg, pollLimit)) {
            Regs.emplace_back(reg);
            return true;
        }
        return false;
    }

    if (std::chrono::steady_clock::now() - PollStart > pollLimit) {
        return false;
    }

    if (DeviceWasDisconnected && !DeviceIsConnected) {
        reg->SetError(ST_UNKNOWN_ERROR);
        Regs.emplace_back(reg);
        return true;
    }

    // Reading range is not supported read one by one
    reg->Device()->ReadRegister(reg);
    Regs.emplace_back(reg);
    return true;
}

void TRegisterReader::Read()
{
    if (RegisterRange) {
        Regs.back()->Device()->ReadRegisterRange(RegisterRange);
    }
}

std::list<PRegister>& TRegisterReader::GetRegisters()
{
    return Regs;
}

bool TRegisterReader::GetDeviceWasDisconnected() const
{
    return DeviceWasDisconnected;
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
