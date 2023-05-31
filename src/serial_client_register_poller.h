#pragma once

#include "poll_plan.h"
#include "port.h"
#include "register.h"
#include "serial_client_device_access_handler.h"

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

struct TRegisterComparePredicate
{
    bool operator()(const PRegister& r1, const PRegister& r2) const;
};

class TThrottlingStateLogger
{
    bool FirstTime;

public:
    TThrottlingStateLogger();

    std::string GetMessage(TThrottlingState state);
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
    typedef std::function<void(PSerialDevice dev)> TDeviceCallback;

    TSerialClientRegisterPoller(size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());

    void PrepareRegisterRanges(const std::list<PRegister>& regList, std::chrono::steady_clock::time_point currentTime);
    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime, TRegisterCallback callback);
    TPollResult OpenPortCycle(TPort& port,
                              const util::TSpentTimeMeter& spentTime,
                              std::chrono::milliseconds maxPollingTime,
                              bool readAtLeastOneRegister,
                              TSerialClientDeviceAccessHandler& lastAccessedDevice,
                              TRegisterCallback callback);
    void SetDeviceDisconnectedCallback(TDeviceCallback callback);
    void DeviceDisconnected(PSerialDevice device, std::chrono::steady_clock::time_point currentTime);

private:
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);

    std::list<PRegister> RegList;

    TDeviceCallback DeviceDisconnectedCallback;

    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    TThrottlingStateLogger ThrottlingStateLogger;
};
