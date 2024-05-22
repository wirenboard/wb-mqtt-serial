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

enum TClientTaskType
{
    POLLING,
    EVENTS
};

class TSerialClientRegisterAndEventsReader: public util::TNonCopyable
{
public:
    typedef std::function<void(PRegister reg)> TRegisterCallback;
    typedef std::function<void(PSerialDevice dev)> TDeviceCallback;

    TSerialClientRegisterAndEventsReader(const std::list<PSerialDevice>& devices,
                                         std::chrono::milliseconds readEventsPeriod,
                                         util::TGetNowFn nowFn,
                                         size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());

    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime,
                         TRegisterCallback regCallback,
                         TDeviceCallback deviceConnectionStateChangedCallback);
    PSerialDevice OpenPortCycle(TPort& port,
                                TRegisterCallback regCallback,
                                TDeviceCallback deviceConnectionStateChangedCallback,
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

class TSerialClient: public std::enable_shared_from_this<TSerialClient>, util::TNonCopyable
{
public:
    typedef std::function<void(PRegister reg)> TRegisterCallback;
    typedef std::function<void(PSerialDevice dev)> TDeviceCallback;

    TSerialClient(PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  util::TGetNowFn nowFn,
                  size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());
    ~TSerialClient();

    void AddDevice(PSerialDevice device);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TRegisterCallback& callback);
    void SetErrorCallback(const TRegisterCallback& callback);
    void SetDeviceConnectionStateChangedCallback(const TDeviceCallback& callback);
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
    std::list<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    TRegisterCallback RegisterReadCallback;
    TRegisterCallback RegisterErrorCallback;
    TDeviceCallback DeviceConnectionStateChangedCallback;
    PBinarySemaphore FlushNeeded;
    PBinarySemaphoreSignal RegisterUpdateSignal, RPCSignal;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

    PRPCRequestHandler RPCRequestHandler;

    std::unique_ptr<TSerialClientDeviceAccessHandler> LastAccessedDevice;
    std::unique_ptr<TSerialClientRegisterAndEventsReader> RegReader;

    util::TGetNowFn NowFn;

    size_t LowPriorityRateLimit;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
