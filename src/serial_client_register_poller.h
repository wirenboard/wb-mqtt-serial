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
    void ClosedPortCycle(std::chrono::steady_clock::time_point currentTime);
    TPollResult OpenPortCycle(TPort& port,
                              std::chrono::steady_clock::time_point currentTime,
                              std::chrono::milliseconds maxPollingTime,
                              bool readAtLeastOneRegister,
                              TSerialClientDeviceAccessHandler& lastAccessedDevice);
    void SetReadCallback(TRegisterCallback callback);
    void SetErrorCallback(TRegisterCallback callback);
    void SetDeviceDisconnectedCallback(TDeviceCallback callback);
    void DeviceDisconnected(PSerialDevice device);

private:
    void ProcessPolledRegister(PRegister reg);
    void SetReadError(PRegister reg);
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);
    void SetDeadline(std::chrono::steady_clock::time_point deadline);

    std::list<PRegister> RegList;

    TRegisterCallback ReadCallback;
    TRegisterCallback ErrorCallback;
    TDeviceCallback DeviceDisconnectedCallback;

    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    TThrottlingStateLogger ThrottlingStateLogger;
};
