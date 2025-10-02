#pragma once

#include <map>

#include "poll_plan.h"
#include "pollable_device.h"
#include "port/port.h"
#include "serial_client_device_access_handler.h"

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

struct TPollableDeviceComparePredicate
{
    bool operator()(const PPollableDevice& d1, const PPollableDevice& d2) const;
};

class TThrottlingStateLogger
{
    bool FirstTime;

public:
    TThrottlingStateLogger();

    std::string GetMessage();
};

struct TPollResult
{
    PSerialDevice Device;
    bool NotEnoughTime = false;
    std::chrono::steady_clock::time_point Deadline;
};

class TSerialClientRegisterPoller
{
public:
    typedef std::function<void(PRegister reg)> TRegisterCallback;

    TSerialClientRegisterPoller(size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());

    void SetDevices(const std::list<PSerialDevice>& devices, std::chrono::steady_clock::time_point currentTime);
    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime, TRegisterCallback callback);
    TPollResult OpenPortCycle(TFeaturePort& port,
                              const util::TSpentTimeMeter& spentTime,
                              std::chrono::milliseconds maxPollingTime,
                              bool readAtLeastOneRegister,
                              TSerialClientDeviceAccessHandler& lastAccessedDevice,
                              TRegisterCallback callback);

    void SuspendPoll(PSerialDevice device, std::chrono::steady_clock::time_point currentTime);
    void ResumePoll(PSerialDevice device);

private:
    void ScheduleNextPoll(PPollableDevice device, std::chrono::steady_clock::time_point currentTime);
    std::chrono::steady_clock::time_point GetDeadline(bool lowPriorityRateLimitIsExceeded,
                                                      const util::TSpentTimeMeter& spentTime) const;
    void OnDeviceConnectionStateChanged(PSerialDevice device);
    void RescheduleDisconnectedDevices();
    void RescheduleDevicesWithSpendedPoll(std::chrono::steady_clock::time_point currentTime);

    std::mutex Mutex;

    std::multimap<PSerialDevice, PPollableDevice> Devices;

    TScheduler<PPollableDevice, TPollableDeviceComparePredicate> Scheduler;

    TThrottlingStateLogger ThrottlingStateLogger;

    TRateLimiter LowPriorityRateLimiter;

    std::vector<PSerialDevice> DisconnectedDevicesWaitingForReschedule;

    std::unordered_map<PSerialDevice, std::chrono::steady_clock::time_point> DevicesWithSpendedPoll;
};
