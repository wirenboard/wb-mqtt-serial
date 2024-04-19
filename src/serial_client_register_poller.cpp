#include "serial_client_register_poller.h"
#include <chrono>
#include <iostream>
#include <unistd.h>

#include "log.h"
#include "serial_device.h"

using namespace std::chrono_literals;
using namespace std::chrono;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto MAX_LOW_PRIORITY_LAG = 1s;

    class TRegisterReader
    {
        PRegisterRange RegisterRange;
        milliseconds MaxPollTime;
        PPollableDevice Device;
        bool ReadAtLeastOneRegister;
        steady_clock::time_point CurrentTime;
        TSerialClientDeviceAccessHandler& LastAccessedDevice;

    public:
        TRegisterReader(steady_clock::time_point currentTime,
                        milliseconds maxPollTime,
                        bool readAtLeastOneRegister,
                        TSerialClientDeviceAccessHandler& lastAccessedDevice)
            : MaxPollTime(maxPollTime),
              ReadAtLeastOneRegister(readAtLeastOneRegister),
              CurrentTime(currentTime),
              LastAccessedDevice(lastAccessedDevice)
        {}

        bool operator()(const PPollableDevice& device, TItemAccumulationPolicy policy, milliseconds pollLimit)
        {
            if (Device) {
                return false;
            }
            RegisterRange = device->ReadRegisterRange(policy,
                                                      pollLimit,
                                                      ReadAtLeastOneRegister,
                                                      MaxPollTime,
                                                      CurrentTime,
                                                      LastAccessedDevice);
            Device = device;
            return !RegisterRange->RegisterList().empty();
        }

        PRegisterRange GetRegisterRange() const
        {
            return RegisterRange;
        }

        PPollableDevice GetDevice() const
        {
            return Device;
        }
    };

    class TClosedPortRegisterReader
    {
        std::list<PRegister> Regs;
        steady_clock::time_point CurrentTime;
        PPollableDevice Device;

    public:
        TClosedPortRegisterReader(steady_clock::time_point currentTime): CurrentTime(currentTime)
        {}

        bool operator()(const PPollableDevice& device, TItemAccumulationPolicy policy, milliseconds pollLimit)
        {
            if (Device) {
                return false;
            }
            Regs = device->SetReadError(CurrentTime);
            Device = device;
            return true;
        }

        std::list<PRegister>& GetRegisters()
        {
            return Regs;
        }

        PPollableDevice GetDevice() const
        {
            return Device;
        }

        void ClearRegisters()
        {
            Regs.clear();
            Device.reset();
        }
    };
};

TSerialClientRegisterPoller::TSerialClientRegisterPoller(size_t lowPriorityRateLimit)
    : Scheduler(MAX_LOW_PRIORITY_LAG),
      ThrottlingStateLogger(),
      LowPriorityRateLimiter(lowPriorityRateLimit)
{}

void TSerialClientRegisterPoller::SetDevices(const std::list<PSerialDevice>& devices,
                                             steady_clock::time_point currentTime)
{
    for (const auto& dev: devices) {
        auto pollableDevice = std::make_shared<TPollableDevice>(dev, currentTime, TPriority::High);
        if (pollableDevice->HasRegisters()) {
            Scheduler.AddEntry(pollableDevice, currentTime, TPriority::High);
            Devices.push_back(pollableDevice);
        }
        pollableDevice = std::make_shared<TPollableDevice>(dev, currentTime, TPriority::Low);
        if (pollableDevice->HasRegisters()) {
            Scheduler.AddEntry(pollableDevice, currentTime, TPriority::Low);
            Devices.push_back(pollableDevice);
        }
    }
}

void TSerialClientRegisterPoller::ScheduleNextPoll(PPollableDevice device)
{
    if (device->HasRegisters()) {
        Scheduler.AddEntry(device, device->GetDeadline(), device->GetPriority());
    }
}

void TSerialClientRegisterPoller::ClosedPortCycle(steady_clock::time_point currentTime, TRegisterCallback callback)
{
    Scheduler.ResetLoadBalancing();

    TClosedPortRegisterReader reader(currentTime);
    do {
        reader.ClearRegisters();
        Scheduler.AccumulateNext(currentTime, reader, false);
        for (auto& reg: reader.GetRegisters()) {
            reg->SetError(TRegister::TError::ReadError);
            if (callback) {
                callback(reg);
            }
        }
        if (reader.GetDevice()) {
            ScheduleNextPoll(reader.GetDevice());
            reader.GetDevice()->GetDevice()->SetTransferResult(false);
        }
    } while (!reader.GetRegisters().empty());
}

std::chrono::steady_clock::time_point TSerialClientRegisterPoller::GetDeadline(
    bool blockLowPriority,
    const util::TSpentTimeMeter& spentTime) const
{
    if (Scheduler.IsEmpty()) {
        return spentTime.GetStartTime() + 1s;
    }
    if (blockLowPriority) {
        auto lowPriorityDeadline = Scheduler.GetLowPriorityDeadline();
        // There are some low priority items
        if (lowPriorityDeadline != std::chrono::steady_clock::time_point::max()) {
            lowPriorityDeadline = std::max(lowPriorityDeadline, LowPriorityRateLimiter.GetStartTime() + 1s);
        }
        return std::min(Scheduler.GetHighPriorityDeadline(), lowPriorityDeadline);
    }
    return Scheduler.GetDeadline();
}

TPollResult TSerialClientRegisterPoller::OpenPortCycle(TPort& port,
                                                       const util::TSpentTimeMeter& spentTime,
                                                       std::chrono::milliseconds maxPollingTime,
                                                       bool readAtLeastOneRegister,
                                                       TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                       TRegisterCallback callback)
{
    TPollResult res;

    TRegisterReader reader(spentTime.GetStartTime(), maxPollingTime, readAtLeastOneRegister, lastAccessedDevice);

    auto blockLowPriority = LowPriorityRateLimiter.IsOverLimit(spentTime.GetStartTime());

    Scheduler.AccumulateNext(spentTime.GetStartTime(), reader, blockLowPriority);
    if (blockLowPriority) {
        auto throttlingMsg = ThrottlingStateLogger.GetMessage();
        if (!throttlingMsg.empty()) {
            LOG(Warn) << port.GetDescription() << " " << throttlingMsg;
        }
    }
    auto range = reader.GetRegisterRange();

    if (!range) {
        // Nothing to read
        res.Deadline = GetDeadline(blockLowPriority, spentTime);
        return res;
    }

    // There are registers waiting read, but they don't fit in allowed poll limit
    if (range->RegisterList().empty()) {
        res.NotEnoughTime = true;
        if (reader.GetDevice()->GetPriority() == TPriority::High) {
            // High priority registers are limited by maxPollingTime
            res.Deadline = spentTime.GetStartTime() + maxPollingTime;
        } else {
            // Low priority registers are limited by high priority and maxPollingTime
            res.Deadline = std::min(Scheduler.GetHighPriorityDeadline(), spentTime.GetStartTime() + maxPollingTime);
        }
        return res;
    }

    res.Device = reader.GetDevice()->GetDevice();
    bool deviceWasConnected = !res.Device->GetIsDisconnected();

    for (auto& reg: range->RegisterList()) {
        if (callback) {
            callback(reg);
        }
        if (reader.GetDevice()->GetPriority() == TPriority::Low) {
            LowPriorityRateLimiter.NewItem(spentTime.GetStartTime());
        }
    }
    ScheduleNextPoll(reader.GetDevice());

    if (deviceWasConnected && res.Device->GetIsDisconnected()) {
        DeviceDisconnected(res.Device, spentTime.GetStartTime());
        if (DeviceDisconnectedCallback) {
            DeviceDisconnectedCallback(res.Device);
        }
    }

    Scheduler.UpdateSelectionTime(ceil<milliseconds>(spentTime.GetSpentTime()), reader.GetDevice()->GetPriority());
    res.Deadline = GetDeadline(blockLowPriority, spentTime);
    return res;
}

void TSerialClientRegisterPoller::DeviceDisconnected(PSerialDevice device,
                                                     std::chrono::steady_clock::time_point currentTime)
{
    for (auto& dev: Devices) {
        if (dev->GetDevice() == device) {
            dev->RescheduleAllRegisters(currentTime);
            return;
        }
    }
}

void TSerialClientRegisterPoller::SetDeviceDisconnectedCallback(TDeviceCallback deviceDisconnectedCallback)
{
    DeviceDisconnectedCallback = deviceDisconnectedCallback;
}

bool TPollableDeviceComparePredicate::operator()(const PPollableDevice& d1, const PPollableDevice& d2) const
{
    if (d1->GetDevice() != d2->GetDevice()) {
        return d1->GetDevice()->DeviceConfig()->SlaveId > d2->GetDevice()->DeviceConfig()->SlaveId;
    }
    return false;
}

TThrottlingStateLogger::TThrottlingStateLogger(): FirstTime(true)
{}

std::string TThrottlingStateLogger::GetMessage()
{
    if (FirstTime) {
        FirstTime = false;
        return "Register read rate limit is exceeded";
    }
    return std::string();
}
