#pragma once

#include "poll_plan.h"
#include "serial_client_device_access_handler.h"
#include "serial_device.h"

struct TRegisterComparePredicate
{
    bool operator()(const PRegister& r1, const PRegister& r2) const;
};

class TPollableDevice
{
    PSerialDevice Device;
    TPriorityQueueSchedule<PRegister, TRegisterComparePredicate> Registers;
    TPriority Priority;

    bool AddRegister(PRegisterRange registerRange,
                     PRegister reg,
                     bool readAtLeastOneRegister,
                     TItemAccumulationPolicy policy,
                     std::chrono::milliseconds pollLimit,
                     std::chrono::milliseconds maxPollTime);

    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point currentTime);

public:
    TPollableDevice(PSerialDevice device, std::chrono::steady_clock::time_point currentTime, TPriority priority);

    PRegisterRange ReadRegisterRange(TItemAccumulationPolicy policy,
                                     std::chrono::milliseconds pollLimit,
                                     bool readAtLeastOneRegister,
                                     std::chrono::milliseconds maxPollTime,
                                     std::chrono::steady_clock::time_point currentTime,
                                     TSerialClientDeviceAccessHandler& lastAccessedDevice);

    std::list<PRegister> SetReadError(std::chrono::steady_clock::time_point currentTime);

    std::chrono::steady_clock::time_point GetDeadline() const;

    TPriority GetPriority() const;

    PSerialDevice GetDevice() const;

    bool HasRegisters() const;

    void RescheduleAllRegisters(std::chrono::steady_clock::time_point currentTime);
};

typedef std::shared_ptr<TPollableDevice> PPollableDevice;
