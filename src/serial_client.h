#pragma once

#include "register.h"
#include <condition_variable>
#include <list>

#include "log.h"
#include "poll_plan.h"
#include "register_handler.h"
#include "serial_client_device_access_handler.h"
#include "serial_client_events_reader.h"
#include "serial_client_register_poller.h"

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

class ISerialClientTask
{
public:
    enum class TRunResult
    {
        OK,
        RETRY
    };

    virtual ~ISerialClientTask() = default;

    virtual ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) = 0;
};

typedef std::shared_ptr<ISerialClientTask> PSerialClientTask;

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

    void AddTask(PSerialClientTask task);

private:
    void Activate();
    void Connect();
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point now,
                             std::chrono::steady_clock::time_point waitUntil);
    PRegisterHandler GetHandler(PRegister) const;
    void ClosedPortCycle();
    void OpenPortCycle();
    void ProcessPolledRegister(PRegister reg);

    PPort Port;
    std::list<PRegister> RegList;
    std::list<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    TRegisterCallback RegisterReadCallback;
    TRegisterCallback RegisterErrorCallback;
    TDeviceCallback DeviceConnectionStateChangedCallback;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

    std::unique_ptr<TSerialClientDeviceAccessHandler> LastAccessedDevice;
    std::unique_ptr<TSerialClientRegisterAndEventsReader> RegReader;

    util::TGetNowFn NowFn;

    size_t LowPriorityRateLimit;

    std::mutex TasksMutex;
    std::condition_variable TasksCv;
    std::vector<PSerialClientTask> Tasks;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
