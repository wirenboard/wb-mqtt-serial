#pragma once

#include "common_utils.h"
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

    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point currentTime);

public:
    TPollableDevice(PSerialDevice device, std::chrono::steady_clock::time_point currentTime, TPriority priority);

    PRegisterRange ReadRegisterRange(TPort& port,
                                     std::chrono::milliseconds pollLimit,
                                     bool readAtLeastOneRegister,
                                     const util::TSpentTimeMeter& sessionTime,
                                     TSerialClientDeviceAccessHandler& lastAccessedDevice);

    std::list<PRegister> MarkWaitingRegistersAsReadErrorAndReschedule(
        std::chrono::steady_clock::time_point currentTime);

    std::chrono::steady_clock::time_point GetDeadline() const;

    TPriority GetPriority() const;

    PSerialDevice GetDevice() const;

    bool HasRegisters() const;

    void RescheduleAllRegisters();
};

typedef std::shared_ptr<TPollableDevice> PPollableDevice;
