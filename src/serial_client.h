#pragma once

#include "register.h"
#include <atomic>
#include <condition_variable>
#include <deque>
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

    TSerialClientRegisterAndEventsReader(const std::list<PSerialDevice>& devices,
                                         std::chrono::milliseconds readEventsPeriod,
                                         util::TGetNowFn nowFn,
                                         util::TGetSystemTimeFn systemTimeFn = std::chrono::system_clock::now,
                                         size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());

    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime, TRegisterCallback regCallback);
    PSerialDevice OpenPortCycle(TFeaturePort& port,
                                TRegisterCallback regCallback,
                                TSerialClientDeviceAccessHandler& lastAccessedDevice);

    std::chrono::steady_clock::time_point GetDeadline(std::chrono::steady_clock::time_point currentTime) const;

    PSerialClientEventsReader GetEventsReader() const;

    void SuspendPoll(PSerialDevice device, std::chrono::steady_clock::time_point currentTime);
    void ResumePoll(PSerialDevice device);

private:
    PSerialClientEventsReader EventsReader;
    TSerialClientRegisterPoller RegisterPoller;
    TScheduler<TClientTaskType> TimeBalancer;
    std::chrono::milliseconds ReadEventsPeriod;

    util::TSpentTimeMeter SpentTime;
    bool LastCycleWasTooSmallToPoll;
    util::TGetNowFn NowFn;
    util::TGetSystemTimeFn SystemTimeFn;
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

    /**
     * @brief Executes some code in the serial client thread.
     *
     * @param port The port to be used for communication.
     * @param lastAccessedDevice A reference to the handler for the last accessed device.
     * @param polledDevices A list of serial devices polled on this port.
     * @return The result of the task execution as a TRunResult.
     */
    virtual ISerialClientTask::TRunResult Run(PFeaturePort port,
                                              TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                              const std::list<PSerialDevice>& polledDevices) = 0;

    //! Only raises the flag, the task decides in Run what to do
    void Cancel();

    bool IsCancelled() const;

private:
    std::atomic<bool> Cancelled{false};
};

typedef std::shared_ptr<ISerialClientTask> PSerialClientTask;

class TSerialClient: public std::enable_shared_from_this<TSerialClient>, util::TNonCopyable
{
public:
    typedef std::function<void(PRegister reg)> TRegisterCallback;

    TSerialClient(PFeaturePort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  util::TGetNowFn nowFn,
                  size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());
    ~TSerialClient();

    void AddDevice(PSerialDevice device);
    void Cycle();
    //! Thread-safe
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TRegisterCallback& callback);
    void SetErrorCallback(const TRegisterCallback& callback);

    PFeaturePort GetPort();
    std::list<PSerialDevice> GetDevices();

    //! Thread-safe. A task added after RequestStop is cancelled at once
    void AddTask(PSerialClientTask task);

    //! Thread-safe. Stops the polling and cancels the queued tasks, writes to the controls are not taken after it
    void RequestStop();

    //! Thread-safe. Returns false if the client still polls or runs a task which is not cancelled at the deadline
    bool WaitStopped(std::chrono::steady_clock::time_point deadline);

    //! Thread-safe
    void Resume();

    //! Thread-safe
    //! @throws std::runtime_error if the polling is not started
    void SuspendPoll(PSerialDevice device, std::chrono::steady_clock::time_point currentTime);

    //! Thread-safe
    //! @throws std::runtime_error if the polling is not started
    void ResumePoll(PSerialDevice device);

private:
    void Activate();
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point now,
                             std::chrono::steady_clock::time_point waitUntil);
    PRegisterHandler GetHandler(PRegister) const;
    void ClosedPortCycle();
    void OpenPortCycle();
    bool IsStopRequested();
    void ProcessPolledRegister(PRegister reg);

    PFeaturePort Port;
    std::list<PRegister> RegList;
    std::list<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    TRegisterCallback RegisterReadCallback;
    TRegisterCallback RegisterErrorCallback;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

    std::unique_ptr<TSerialClientDeviceAccessHandler> LastAccessedDevice;
    std::unique_ptr<TSerialClientRegisterAndEventsReader> RegReader;
    std::mutex RegReaderMutex;

    util::TGetNowFn NowFn;

    size_t LowPriorityRateLimit;

    std::mutex TasksMutex;
    std::condition_variable TasksCv;
    std::deque<PSerialClientTask> Tasks;
    bool StopRequested = false;
    bool Stopped = false;
    std::condition_variable StoppedCv;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
