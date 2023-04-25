#include "serial_client.h"
#include <chrono>
#include <iostream>
#include <unistd.h>

#include "common_utils.h"
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
        auto sendByteTime = port.GetSendTime(1);
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

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices,
                             PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             size_t lowPriorityRateLimit)
    : Port(port),
      Active(false),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      EventsReader(MAX_EVENT_READ_ERRORS),
      RegisterPoller(lowPriorityRateLimit),
      LastAccessedDevice(EventsReader),
      TimeBalancer(BALANCING_THRESHOLD, 0)
{
    FlushNeeded = std::make_shared<TBinarySemaphore>();
    RPCRequestHandler = std::make_shared<TRPCRequestHandler>();
    RegisterUpdateSignal = FlushNeeded->MakeSignal();
    RPCSignal = FlushNeeded->MakeSignal();
    RegisterPoller.SetDeviceDisconnectedCallback(
        [this](PSerialDevice device) { EventsReader.DeviceDisconnected(device); });
    ReadEventsPeriod = GetReadEventsPeriod(*Port);
}

TSerialClient::~TSerialClient()
{
    Active = false;
    if (Port->IsOpen()) {
        Port->Close();
    }
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg);
    RegList.push_back(reg);
    EventsReader.AddRegister(reg);
    LOG(Debug) << "AddRegister: " << reg;
}

void TSerialClient::Activate()
{
    if (!Active) {
        Active = true;
        auto now = steady_clock::now();
        RegisterPoller.PrepareRegisterRanges(RegList, now);
        if (EventsReader.HasRegisters()) {
            TimeBalancer.AddEntry(TClientTaskType::EVENTS, now, TPriority::High);
        }
        TimeBalancer.AddEntry(TClientTaskType::POLLING, now, TPriority::Low);
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
        if (LastAccessedDevice.PrepareToAccess(handler->Device())) {
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

void TSerialClient::WaitForPollAndFlush(steady_clock::time_point now, steady_clock::time_point waitUntil)
{
    if (now > waitUntil) {
        waitUntil = now;
    }

    if (Debug.IsEnabled()) {
        LOG(Debug) << Port->GetDescription() << duration_cast<milliseconds>(now.time_since_epoch()).count()
                   << ": Wait until " << duration_cast<milliseconds>(waitUntil.time_since_epoch()).count();
    }

    // Limit waiting time to be responsive
    waitUntil = std::min(waitUntil, now + MAX_POLL_TIME);
    while (FlushNeeded->Wait(waitUntil)) {
        if (FlushNeeded->GetSignalValue(RegisterUpdateSignal)) {
            DoFlush();
        }
        if (FlushNeeded->GetSignalValue(RPCSignal)) {
            // End session with current device to make bus clean for RPC
            LastAccessedDevice.PrepareToAccess(nullptr);
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
    auto wait_until = std::chrono::steady_clock::now() + CLOSED_PORT_CYCLE_TIME;

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

    EventsReader.SetReadErrors([this](PRegister reg) { ProcessPolledRegister(reg); });
    RegisterPoller.ClosedPortCycle(wait_until);
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
    FlushNeeded->Signal(RegisterUpdateSignal);
}

void TSerialClient::SetReadCallback(const TSerialClient::TCallback& callback)
{
    ReadCallback = callback;
    RegisterPoller.SetReadCallback(callback);
}

void TSerialClient::SetErrorCallback(const TSerialClient::TCallback& callback)
{
    ErrorCallback = callback;
    RegisterPoller.SetErrorCallback(callback);
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
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

void TSerialClient::OpenPortCycle()
{
    UpdateFlushNeeded();
    auto now = steady_clock::now();
    WaitForPollAndFlush(now, TimeBalancer.GetDeadline(now));

    util::TSpendTimeMeter spendTime;
    spendTime.Start();

    TSerialClientTaskHandler handler;
    TimeBalancer.AccumulateNext(spendTime.GetStartTime(), handler);
    if (handler.NotReady) {
        return;
    }

    if (handler.TaskType == TClientTaskType::EVENTS) {
        if (EventsReader.ReadEvents(
                *Port,
                MAX_POLL_TIME,
                [this](PRegister reg) { ProcessPolledRegister(reg); },
                [this](PSerialDevice device) {
                    device->SetDisconnected();
                    RegisterPoller.DeviceDisconnected(device);
                }))
        {
            TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(spendTime.GetSpendTime()), TPriority::High);
        }
        TimeBalancer.AddEntry(TClientTaskType::EVENTS, spendTime.GetStartTime() + ReadEventsPeriod, TPriority::High);
    } else {
        auto device = RegisterPoller.OpenPortCycle(*Port,
                                                   spendTime.GetStartTime(),
                                                   std::min(handler.PollLimit, MAX_POLL_TIME),
                                                   handler.Policy == TItemAccumulationPolicy::Force,
                                                   LastAccessedDevice);
        TimeBalancer.AddEntry(TClientTaskType::POLLING, RegisterPoller.GetDeadline(), TPriority::Low);
        if (device) {
            TimeBalancer.UpdateSelectionTime(ceil<milliseconds>(spendTime.GetSpendTime()), TPriority::Low);
            OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
        }
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
