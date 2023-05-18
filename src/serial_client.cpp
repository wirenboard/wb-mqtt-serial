#include "serial_client.h"
#include <chrono>
#include <iostream>
#include <unistd.h>

#include "modbus_ext_common.h"

using namespace std::chrono_literals;
using namespace std::chrono;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto PORT_OPEN_ERROR_NOTIFICATION_INTERVAL = 5min;
    const auto CLOSED_PORT_CYCLE_TIME = 500ms;
    const auto MAX_POLL_TIME = 100ms;
    const auto MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;
    const auto BALANCING_THRESHOLD = 500ms;
    const auto MIN_READ_EVENTS_TIME = 25ms;
    const size_t MAX_EVENT_READ_ERRORS = 10;

    std::chrono::milliseconds GetReadEventsPeriod(const TPort& port)
    {
        auto sendByteTime = port.GetSendTimeBytes(1);
        // >= 115200
        if (sendByteTime < 100us) {
            return 50ms;
        }
        // >= 38400
        if (sendByteTime < 300us) {
            return 100ms;
        }
        // < 38400
        return 200ms;
    }
};

TSerialClient::TSerialClient(PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             util::TGetNowFn nowFn,
                             size_t lowPriorityRateLimit)
    : Port(port),
      OpenCloseLogic(openCloseSettings, nowFn),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      NowFn(nowFn),
      LowPriorityRateLimit(lowPriorityRateLimit)
{
    FlushNeeded = std::make_shared<TBinarySemaphore>();
    RPCRequestHandler = std::make_shared<TRPCRequestHandler>();
    RegisterUpdateSignal = FlushNeeded->MakeSignal();
    RPCSignal = FlushNeeded->MakeSignal();
}

TSerialClient::~TSerialClient()
{
    if (Port->IsOpen()) {
        Port->Close();
    }
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (RegReader)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg);
    RegList.push_back(reg);
    LOG(Debug) << "AddRegister: " << reg;
}

void TSerialClient::Activate()
{
    if (!RegReader) {
        RegReader = std::make_unique<TSerialClientRegisterAndEventsReader>(RegList,
                                                                           GetReadEventsPeriod(*Port),
                                                                           NowFn,
                                                                           LowPriorityRateLimit);
        LastAccessedDevice = std::make_unique<TSerialClientDeviceAccessHandler>(RegReader->GetEventsReader());
    }
}

void TSerialClient::Connect()
{
    OpenCloseLogic.OpenIfAllowed(Port);
}

void TSerialClient::DoFlush()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        if (LastAccessedDevice->PrepareToAccess(handler->Device())) {
            handler->Flush();
        } else {
            reg->SetError(TRegister::TError::WriteError);
        }
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

void TSerialClient::WaitForPollAndFlush(steady_clock::time_point currentTime, steady_clock::time_point waitUntil)
{
    if (currentTime > waitUntil) {
        waitUntil = currentTime;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription() << duration_cast<milliseconds>(currentTime.time_since_epoch()).count()
                   << ": Wait until " << duration_cast<milliseconds>(waitUntil.time_since_epoch()).count();
    }

    // Limit waiting time to be responsive
    waitUntil = std::min(waitUntil, currentTime + MAX_POLL_TIME);
    while (FlushNeeded->Wait(waitUntil)) {
        if (FlushNeeded->GetSignalValue(RegisterUpdateSignal)) {
            DoFlush();
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            // End session with current device to make bus clean for RPC
            LastAccessedDevice->PrepareToAccess(nullptr);
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

void TSerialClient::ClosedPortCycle()
{
    auto wait_until = NowFn() + CLOSED_PORT_CYCLE_TIME;

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

    RegReader->ClosedPortCycle(wait_until, [this](PRegister reg) { ProcessPolledRegister(reg); });
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

void TSerialClient::OpenPortCycle()
{
    UpdateFlushNeeded();
    auto currentTime = NowFn();
    WaitForPollAndFlush(currentTime, RegReader->GetDeadline(currentTime));

    auto device = RegReader->OpenPortCycle(
        *Port,
        [this](PRegister reg) { ProcessPolledRegister(reg); },
        *LastAccessedDevice);

    if (device) {
        OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    }
}

PPort TSerialClient::GetPort()
{
    return Port;
}

void TSerialClient::RPCTransceive(PRPCRequest request) const
{
    RPCRequestHandler->RPCTransceive(request, FlushNeeded, RPCSignal);
}

TSerialClientRegisterAndEventsReader::TSerialClientRegisterAndEventsReader(const std::list<PRegister>& regList,
                                                                           std::chrono::milliseconds readEventsPeriod,
                                                                           util::TGetNowFn nowFn,
                                                                           size_t lowPriorityRateLimit)
    : EventsReader(MAX_EVENT_READ_ERRORS),
      RegisterPoller(lowPriorityRateLimit),
      TimeBalancer(BALANCING_THRESHOLD, 0),
      ReadEventsPeriod(readEventsPeriod),
      SpentTime(nowFn),
      LastCycleWasTooSmallToPoll(false),
      NowFn(nowFn)
{
    auto currentTime = NowFn();
    RegisterPoller.SetDeviceDisconnectedCallback(
        [this](PSerialDevice device) { EventsReader.DeviceDisconnected(device); });
    RegisterPoller.PrepareRegisterRanges(regList, currentTime);
    for (const auto& reg: regList) {
        EventsReader.AddRegister(reg);
    }
    TimeBalancer.AddEntry(TClientTaskType::POLLING, currentTime, TPriority::Low);
}

void TSerialClientRegisterAndEventsReader::ClosedPortCycle(std::chrono::steady_clock::time_point currentTime,
                                                           TCallback regCallback)
{
    EventsReader.SetReadErrors(regCallback);
    RegisterPoller.ClosedPortCycle(currentTime, regCallback);
}

class TSerialClientTaskHandler
{
public:
    TClientTaskType TaskType;
    TItemAccumulationPolicy Policy;
    milliseconds PollLimit;
    bool NotReady = true;

    bool operator()(TClientTaskType task, TItemAccumulationPolicy policy, milliseconds pollLimit)
    {
        PollLimit = pollLimit;
        TaskType = task;
        Policy = policy;
        NotReady = false;
        return true;
    }
};

PSerialDevice TSerialClientRegisterAndEventsReader::OpenPortCycle(TPort& port,
                                                                  TCallback regCallback,
                                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    // Count idle time as high priority task time to faster reach time balancing threshold
    if (LastCycleWasTooSmallToPoll) {
        TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::High);
    }

    SpentTime.Start();
    TSerialClientTaskHandler handler;
    TimeBalancer.AccumulateNext(SpentTime.GetStartTime(), handler);
    if (handler.NotReady) {
        return nullptr;
    }

    if (handler.TaskType == TClientTaskType::EVENTS) {
        if (EventsReader.HasDevicesWithEnabledEvents()) {
            lastAccessedDevice.PrepareToAccess(nullptr);
            EventsReader.ReadEvents(
                port,
                MAX_POLL_TIME,
                regCallback,
                [this](PSerialDevice device) {
                    device->SetDisconnected();
                    RegisterPoller.DeviceDisconnected(device, NowFn());
                },
                NowFn);
            TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::High);
            TimeBalancer.AddEntry(TClientTaskType::EVENTS,
                                  SpentTime.GetStartTime() + ReadEventsPeriod,
                                  TPriority::High);
        }
        SpentTime.Start();
        return nullptr;
    }

    // Some registers can have theoretical read time more than poll limit.
    // Define special cases when reading can exceed poll limit to read the registers:
    // 1. TimeBalancer can force reading of such registers.
    // 2. If there are not devices with enabled events, the only limiting timeout is MAX_POLL_TIME.
    //    We can miss it and read at least one register.
    const bool readAtLeastOneRegister =
        (handler.Policy == TItemAccumulationPolicy::Force) || !EventsReader.HasDevicesWithEnabledEvents();

    auto res = RegisterPoller.OpenPortCycle(port,
                                            SpentTime,
                                            std::min(handler.PollLimit, MAX_POLL_TIME),
                                            readAtLeastOneRegister,
                                            lastAccessedDevice,
                                            regCallback);
    TimeBalancer.AddEntry(TClientTaskType::POLLING, res.Deadline, TPriority::Low);
    if (res.NotEnoughTime) {
        LastCycleWasTooSmallToPoll = true;
    } else {
        LastCycleWasTooSmallToPoll = false;
        TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(SpentTime.GetSpentTime()), TPriority::Low);
    }

    if (EventsReader.HasDevicesWithEnabledEvents() && !TimeBalancer.Contains(TClientTaskType::EVENTS)) {
        TimeBalancer.AddEntry(TClientTaskType::EVENTS, SpentTime.GetStartTime() + ReadEventsPeriod, TPriority::High);
    }

    SpentTime.Start();
    return res.Device;
}

std::chrono::steady_clock::time_point TSerialClientRegisterAndEventsReader::GetDeadline(
    std::chrono::steady_clock::time_point currentTime) const
{
    return TimeBalancer.GetDeadline(currentTime);
}

TSerialClientEventsReader& TSerialClientRegisterAndEventsReader::GetEventsReader()
{
    return EventsReader;
}
