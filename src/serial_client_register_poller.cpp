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

    class TDeviceReader
    {
        PRegisterRange RegisterRange;
        milliseconds MaxPollTime;
        PPollableDevice Device;
        bool ReadAtLeastOneRegister;
        steady_clock::time_point CurrentTime;
        TSerialClientDeviceAccessHandler& LastAccessedDevice;

    public:
        TDeviceReader(steady_clock::time_point currentTime,
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

            pollLimit = std::min(MaxPollTime, pollLimit);
            if (policy != TItemAccumulationPolicy::Force) {
                ReadAtLeastOneRegister = false;
            }

            RegisterRange =
                device->ReadRegisterRange(pollLimit, ReadAtLeastOneRegister, CurrentTime, LastAccessedDevice);
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

    class TClosedPortDeviceReader
    {
        std::list<PRegister> Regs;
        steady_clock::time_point CurrentTime;
        PPollableDevice Device;

    public:
        TClosedPortDeviceReader(steady_clock::time_point currentTime): CurrentTime(currentTime)
        {}

        bool operator()(const PPollableDevice& device, TItemAccumulationPolicy policy, milliseconds pollLimit)
        {
            if (Device) {
                return false;
            }
            Regs = device->MarkWaitingRegistersAsReadErrorAndReschedule(CurrentTime);
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
            Devices.insert({dev, pollableDevice});
        }
        pollableDevice = std::make_shared<TPollableDevice>(dev, currentTime, TPriority::Low);
        if (pollableDevice->HasRegisters()) {
            Scheduler.AddEntry(pollableDevice, currentTime, TPriority::Low);
            Devices.insert({dev, pollableDevice});
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

    TClosedPortDeviceReader reader(currentTime);
    do {
        reader.ClearRegisters();
        Scheduler.AccumulateNext(currentTime, reader, TItemSelectionPolicy::All);
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
    TItemSelectionPolicy policy,
    const util::TSpentTimeMeter& spentTime) const
{
    if (Scheduler.IsEmpty()) {
        return spentTime.GetStartTime() + 1s;
    }
    if (policy == TItemSelectionPolicy::OnlyHighPriority) {
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

    TDeviceReader reader(spentTime.GetStartTime(), maxPollingTime, readAtLeastOneRegister, lastAccessedDevice);

    auto selectionPolicy = LowPriorityRateLimiter.IsOverLimit(spentTime.GetStartTime())
                               ? TItemSelectionPolicy::OnlyHighPriority
                               : TItemSelectionPolicy::All;

    Scheduler.AccumulateNext(spentTime.GetStartTime(), reader, selectionPolicy);
    if (selectionPolicy == TItemSelectionPolicy::OnlyHighPriority) {
        auto throttlingMsg = ThrottlingStateLogger.GetMessage();
        if (!throttlingMsg.empty()) {
            LOG(Warn) << port.GetDescription() << " " << throttlingMsg;
        }
    }
    auto range = reader.GetRegisterRange();

    if (!range) {
        // Nothing to read
        res.Deadline = GetDeadline(selectionPolicy, spentTime);
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
    res.Deadline = GetDeadline(selectionPolicy, spentTime);
    return res;
}

void TSerialClientRegisterPoller::DeviceDisconnected(PSerialDevice device,
                                                     std::chrono::steady_clock::time_point currentTime)
{
    auto range = Devices.equal_range(device);
    for (auto it = range.first; it != range.second; ++it) {
        it->second->RescheduleAllRegisters(currentTime);
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
