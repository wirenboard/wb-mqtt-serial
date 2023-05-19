#pragma once

#include "binary_semaphore.h"
#include "common_utils.h"
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

class TSerialClientRegisterAndEventsReader: public util::TNonCopyable
{
public:
    typedef std::function<void(PRegister reg)> TCallback;

    TSerialClientRegisterAndEventsReader(const std::list<PRegister>& regList,
                                         std::chrono::milliseconds readEventsPeriod,
                                         util::TGetNowFn nowFn,
                                         size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());

    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime, TCallback regCallback);
    PSerialDevice OpenPortCycle(TPort& port,
                                TCallback regCallback,
                                TSerialClientDeviceAccessHandler& lastAccessedDevice);

    std::chrono::steady_clock::time_point GetDeadline(std::chrono::steady_clock::time_point currentTime) const;

    TSerialClientEventsReader& GetEventsReader();

private:
    TSerialClientEventsReader EventsReader;
    TSerialClientRegisterPoller RegisterPoller;
    TScheduler<TClientTaskType> TimeBalancer;
    std::chrono::milliseconds ReadEventsPeriod;

    util::TSpentTimeMeter SpentTime;
    bool LastCycleWasTooSmallToPoll;
    util::TGetNowFn NowFn;
};

enum TClientTaskType
{
    POLLING,
    EVENTS
};

class TSerialClient: public std::enable_shared_from_this<TSerialClient>, util::TNonCopyable
{
public:
    typedef std::function<void(PRegister reg)> TCallback;

    TSerialClient(PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  util::TGetNowFn nowFn,
                  size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    PPort GetPort();
    void RPCTransceive(PRPCRequest request) const;

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
    void ProcessPolledRegister(PRegister reg);

    PPort Port;
    std::list<PRegister> RegList;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    TCallback ReadCallback;
    TCallback ErrorCallback;
    PBinarySemaphore FlushNeeded;
    PBinarySemaphoreSignal RegisterUpdateSignal, RPCSignal;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

    PRPCRequestHandler RPCRequestHandler;

    std::unique_ptr<TSerialClientDeviceAccessHandler> LastAccessedDevice;
    std::unique_ptr<TSerialClientRegisterAndEventsReader> RegHandler;

    util::TGetNowFn NowFn;

    size_t LowPriorityRateLimit;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
