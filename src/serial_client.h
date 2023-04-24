#pragma once

#include "binary_semaphore.h"
#include "log.h"
#include "modbus_ext_common.h"
#include "poll_plan.h"
#include "register_handler.h"
#include "rpc_request_handler.h"
#include "serial_client_device_access_handler.h"
#include "serial_client_events_reader.h"
#include "serial_client_register_poller.h"
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

enum TClientTaskType
{
    POLLING,
    EVENTS
};

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg)> TCallback;

    TSerialClient(const std::vector<PSerialDevice>& devices,
                  PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    PPort GetPort();
    void RPCTransceive(PRPCRequest request) const;
    void ProcessPolledRegister(PRegister reg);

private:
    void Activate();
    void Connect();
    void DoFlush();
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point now,
                             std::chrono::steady_clock::time_point waitUntil);
    PRegisterHandler GetHandler(PRegister) const;
    void ClosedPortCycle();
    void OpenPortCycle();
    void UpdateFlushNeeded();

    PPort Port;
    std::list<PRegister> RegList;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    TCallback ReadCallback;
    TCallback ErrorCallback;
    PBinarySemaphore FlushNeeded;
    PBinarySemaphoreSignal RegisterUpdateSignal, RPCSignal;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

    PRPCRequestHandler RPCRequestHandler;

    TSerialClientEventsReader EventsReader;
    TSerialClientRegisterPoller RegisterPoller;
    TSerialClientDeviceAccessHandler LastAccessedDevice;
    TScheduler<TClientTaskType> TimeBalancer;
    std::chrono::milliseconds ReadEventsPeriod;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
