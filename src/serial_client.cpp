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
};

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices,
                             PPort port,
                             const TPortOpenCloseLogic::TSettings& openCloseSettings,
                             size_t lowPriorityRateLimit)
    : Port(port),
      Active(false),
      OpenCloseLogic(openCloseSettings),
      ConnectLogger(PORT_OPEN_ERROR_NOTIFICATION_INTERVAL, "[serial client] "),
      RegisterPoller(lowPriorityRateLimit),
      LastAccessedDevice(EventsReader),
      TimeBalancer(BALANCING_THRESHOLD)
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
        RegisterPoller.PrepareRegisterRanges(RegList);
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

    // TODO: Set read error to sporadic registers

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

// |                                   |                  No more than MAX_POLL_TIME                      |
// |                                   |                      |                                           |
// |                                   |         if total read events overhead < BALANCING_THRESHOLD      |
// | Wait till first register to poll  | Up to MAX_POLL_TIME  | MAX_POLL_TIME - time spent reading events |
// | or till next events read time     |                     else                                         |
// |                                   | MIN_READ_EVENTS_TIME | MAX_POLL_TIME - time spent reading events |
// |                                   |                      |                                           |
// |-----------------------------------|----------------------|-------------------------------------------|
// | Process RPC and commands from MQTT|      Read events     |                Poll registers             |
//
void TSerialClient::OpenPortCycle()
{
    auto now = steady_clock::now();
    auto waitTimeout = std::min(RegisterPoller.GetDeadline(now), EventsReader.GetNextEventsReadTime());
    WaitForPollAndFlush(now, waitTimeout);

    auto maxEventsReadTime = MAX_POLL_TIME;
    if (TimeBalancer.ShouldDecrement()) {
        maxEventsReadTime = MIN_READ_EVENTS_TIME;
    }
    auto readEventsStartTime = steady_clock::now();
    EventsReader.ReadEvents(*Port, readEventsStartTime, maxEventsReadTime, [this](PRegister reg) {
        ProcessPolledRegister(reg);
    });

    auto pollingStartTime = steady_clock::now();
    auto readEventsTime = duration_cast<milliseconds>(pollingStartTime - readEventsStartTime);
    TimeBalancer.IncrementTotalTime(readEventsTime);
    auto device =
        RegisterPoller.OpenPortCycle(*Port, pollingStartTime, MAX_POLL_TIME - readEventsTime, LastAccessedDevice);
    TimeBalancer.DecrementTotalTime(duration_cast<milliseconds>(steady_clock::now() - pollingStartTime));

    if (device) {
        OpenCloseLogic.CloseIfNeeded(Port, device->GetIsDisconnected());
    }

    UpdateFlushNeeded();
}

PPort TSerialClient::GetPort()
{
    return Port;
}

void TSerialClient::RPCTransceive(PRPCRequest request) const
{
    RPCRequestHandler->RPCTransceive(request, FlushNeeded, RPCSignal);
}

PRegister TSerialClient::FindRegister(uint8_t slaveId, uint16_t addr) const
{
    for (const auto& reg: RegList) {
        if (std::stoi(reg->Device()->DeviceConfig()->SlaveId) == slaveId &&
            GetUint32RegisterAddress(reg->GetAddress()) == addr)
        {
            return reg;
        }
    }
    return nullptr;
}
