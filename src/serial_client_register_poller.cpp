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
    const auto SUSPEND_POLL_TIMEOUT = 10min;

    const auto DISCONNECTED_POLL_DELAY_STEP = 500ms;
    const auto DISCONNECTED_POLL_DELAY_LIMIT = 10s;

    class TDeviceReader
    {
        PRegisterRange RegisterRange;
        milliseconds MaxPollTime;
        PPollableDevice Device;
        bool ReadAtLeastOneRegister;
        const util::TSpentTimeMeter& SessionTime;
        TSerialClientDeviceAccessHandler& LastAccessedDevice;
        TFeaturePort& Port;

    public:
        TDeviceReader(TFeaturePort& port,
                      const util::TSpentTimeMeter& sessionTime,
                      milliseconds maxPollTime,
                      bool readAtLeastOneRegister,
                      TSerialClientDeviceAccessHandler& lastAccessedDevice)
            : MaxPollTime(maxPollTime),
              ReadAtLeastOneRegister(readAtLeastOneRegister),
              SessionTime(sessionTime),
              LastAccessedDevice(lastAccessedDevice),
              Port(port)
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
                device->ReadRegisterRange(Port, pollLimit, ReadAtLeastOneRegister, SessionTime, LastAccessedDevice);
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
    std::unique_lock lock(Mutex);

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
        dev->AddOnConnectionStateChangedCallback(
            [this](PSerialDevice device) { OnDeviceConnectionStateChanged(device); });
    }
}

void TSerialClientRegisterPoller::ScheduleNextPoll(PPollableDevice device, steady_clock::time_point currentTime)
{
    if (device->HasRegisters()) {
        auto delay = milliseconds(0);
        auto deadline = device->GetDeadline();
        if (device->GetDevice()->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) {
            delay = device->GetDisconnectedPollDelay();
            if (delay.count()) {
                deadline = currentTime + delay;
                LOG(Debug) << "Device " << device->GetDevice()->ToString() << " poll delayed for " << delay.count()
                           << " ms";
            }
            if (delay < DISCONNECTED_POLL_DELAY_LIMIT) {
                delay += DISCONNECTED_POLL_DELAY_STEP;
            }
        }
        device->SetDisconnectedPollDelay(delay);
        Scheduler.AddEntry(device, deadline, device->GetPriority());
    }
}

void TSerialClientRegisterPoller::ClosedPortCycle(steady_clock::time_point currentTime, TRegisterCallback callback)
{
    Scheduler.ResetLoadBalancing();

    RescheduleDisconnectedDevices();
    RescheduleDevicesWithSpendedPoll(currentTime);

    std::unique_lock lock(Mutex);

    TClosedPortDeviceReader reader(currentTime);
    do {
        reader.ClearRegisters();
        Scheduler.AccumulateNext(currentTime, reader, TItemSelectionPolicy::All);
        for (auto& reg: reader.GetRegisters()) {
            reg->SetError(TRegister::TError::ReadError);
            if (callback) {
                callback(reg);
            }
            auto device = reader.GetDevice()->GetDevice();
            device->SetTransferResult(false);
        }
        if (reader.GetDevice()) {
            ScheduleNextPoll(reader.GetDevice(), currentTime);
        }
    } while (!reader.GetRegisters().empty());
}

std::chrono::steady_clock::time_point TSerialClientRegisterPoller::GetDeadline(
    bool lowPriorityRateLimitIsExceeded,
    const util::TSpentTimeMeter& spentTime) const
{
    if (Scheduler.IsEmpty()) {
        return spentTime.GetStartTime() + 1s;
    }
    if (lowPriorityRateLimitIsExceeded) {
        auto lowPriorityDeadline = Scheduler.GetLowPriorityDeadline();
        // There are some low priority items
        if (lowPriorityDeadline != std::chrono::steady_clock::time_point::max()) {
            lowPriorityDeadline = std::max(lowPriorityDeadline, LowPriorityRateLimiter.GetStartTime() + 1s);
        }
        return std::min(Scheduler.GetHighPriorityDeadline(), lowPriorityDeadline);
    }
    return Scheduler.GetDeadline();
}

TPollResult TSerialClientRegisterPoller::OpenPortCycle(TFeaturePort& port,
                                                       const util::TSpentTimeMeter& spentTime,
                                                       std::chrono::milliseconds maxPollingTime,
                                                       bool readAtLeastOneRegister,
                                                       TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                       TRegisterCallback callback)
{
    RescheduleDisconnectedDevices();
    RescheduleDevicesWithSpendedPoll(spentTime.GetStartTime());

    std::unique_lock lock(Mutex);

    TPollResult res;

    TDeviceReader reader(port, spentTime, maxPollingTime, readAtLeastOneRegister, lastAccessedDevice);

    bool lowPriorityRateLimitIsExceeded = LowPriorityRateLimiter.IsOverLimit(spentTime.GetStartTime());

    Scheduler.AccumulateNext(spentTime.GetStartTime(),
                             reader,
                             lowPriorityRateLimitIsExceeded ? TItemSelectionPolicy::OnlyHighPriority
                                                            : TItemSelectionPolicy::All);
    if (lowPriorityRateLimitIsExceeded) {
        auto throttlingMsg = ThrottlingStateLogger.GetMessage();
        if (!throttlingMsg.empty()) {
            LOG(Warn) << port.GetDescription() << " " << throttlingMsg;
        }
    }
    auto range = reader.GetRegisterRange();

    if (!range) {
        // Nothing to read
        res.Deadline = GetDeadline(lowPriorityRateLimitIsExceeded, spentTime);
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

    for (auto& reg: range->RegisterList()) {
        if (callback) {
            callback(reg);
        }
        if (reader.GetDevice()->GetPriority() == TPriority::Low) {
            LowPriorityRateLimiter.NewItem(spentTime.GetStartTime());
        }
    }
    ScheduleNextPoll(reader.GetDevice(), spentTime.GetStartTime());

    Scheduler.UpdateSelectionTime(ceil<milliseconds>(spentTime.GetSpentTime()), reader.GetDevice()->GetPriority());
    res.Deadline = GetDeadline(lowPriorityRateLimitIsExceeded, spentTime);
    return res;
}

void TSerialClientRegisterPoller::SuspendPoll(PSerialDevice device, std::chrono::steady_clock::time_point currentTime)
{
    std::unique_lock lock(Mutex);

    if (DevicesWithSpendedPoll.find(device) == DevicesWithSpendedPoll.end()) {
        auto range = Devices.equal_range(device);
        if (range.first == range.second) {
            throw std::runtime_error("Device " + device->ToString() + " is not included to poll");
        }
        for (auto it = range.first; it != range.second; ++it) {
            Scheduler.Remove(it->second);
        }
    }

    DevicesWithSpendedPoll[device] = currentTime + SUSPEND_POLL_TIMEOUT;
    LOG(Info) << "Device " << device->ToString() << " poll suspended";

    if (device->GetConnectionState() != TDeviceConnectionState::DISCONNECTED) {
        device->SetDisconnected();
    }
}

void TSerialClientRegisterPoller::ResumePoll(PSerialDevice device)
{
    std::unique_lock lock(Mutex);

    if (DevicesWithSpendedPoll.find(device) == DevicesWithSpendedPoll.end()) {
        throw std::runtime_error("Device " + device->ToString() + " poll is not suspended");
    }

    auto range = Devices.equal_range(device);
    for (auto it = range.first; it != range.second; ++it) {
        it->second->RescheduleAllRegisters();
        Scheduler.AddEntry(it->second, it->second->GetDeadline(), it->second->GetPriority());
    }

    DevicesWithSpendedPoll.erase(device);
    LOG(Info) << "Device " << device->ToString() << " poll resumed";
}

void TSerialClientRegisterPoller::OnDeviceConnectionStateChanged(PSerialDevice device)
{
    if (device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED &&
        DevicesWithSpendedPoll.find(device) == DevicesWithSpendedPoll.end())
    {
        DisconnectedDevicesWaitingForReschedule.push_back(device);
    }
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

void TSerialClientRegisterPoller::RescheduleDisconnectedDevices()
{
    std::unique_lock lock(Mutex);
    for (auto& device: DisconnectedDevicesWaitingForReschedule) {
        auto range = Devices.equal_range(device);
        for (auto it = range.first; it != range.second; ++it) {
            Scheduler.Remove(it->second);
            it->second->RescheduleAllRegisters();
            Scheduler.AddEntry(it->second, it->second->GetDeadline(), it->second->GetPriority());
        }
    }
    DisconnectedDevicesWaitingForReschedule.clear();
}

void TSerialClientRegisterPoller::RescheduleDevicesWithSpendedPoll(std::chrono::steady_clock::time_point currentTime)
{
    std::list<PSerialDevice> list;
    {
        std::unique_lock lock(Mutex);
        for (auto it = DevicesWithSpendedPoll.begin(); it != DevicesWithSpendedPoll.end(); ++it) {
            if (it->second <= currentTime) {
                list.push_back(it->first);
            }
        }
    }
    for (auto device: list) {
        ResumePoll(device);
    }
}
