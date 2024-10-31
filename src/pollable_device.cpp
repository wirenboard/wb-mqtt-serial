#include "pollable_device.h"

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    auto cmp = r1->GetAddress().Compare(r2->GetAddress());
    if (cmp != 0) {
        return cmp > 0;
    }
    // addresses are equal, compare offsets
    return r1->GetDataOffset() > r2->GetDataOffset();
}

TPollableDevice::TPollableDevice(PSerialDevice device,
                                 std::chrono::steady_clock::time_point currentTime,
                                 TPriority priority)
    : Device(device),
      Priority(priority)
{
    for (const auto& reg: Device->GetRegisters()) {
        if ((Priority == TPriority::High && reg->IsHighPriority()) ||
            (Priority == TPriority::Low && !reg->IsHighPriority()))
        {
            if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
                Registers.AddEntry(reg, currentTime);
            }
        }
    }
}

PRegisterRange TPollableDevice::ReadRegisterRange(std::chrono::milliseconds pollLimit,
                                                  bool readAtLeastOneRegister,
                                                  const util::TSpentTimeMeter& sessionTime,
                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    auto currentTime = sessionTime.GetStartTime();
    auto registerRange = Device->CreateRegisterRange();
    while (Registers.HasReadyItems(currentTime)) {
        const auto limit = (readAtLeastOneRegister && registerRange->RegisterList().empty())
                               ? std::chrono::milliseconds::max()
                               : pollLimit;
        const auto& item = Registers.GetTop();
        if (!registerRange->Add(item.Data, limit)) {
            break;
        }
        Registers.Pop();
    }

    if (!registerRange->RegisterList().empty()) {
        bool readOk = false;
        if (lastAccessedDevice.PrepareToAccess(Device)) {
            Device->ReadRegisterRange(registerRange);
            readOk = true;
        }

        for (auto& reg: registerRange->RegisterList()) {
            reg->SetLastPollTime(currentTime);
            if (!readOk) {
                reg->SetError(TRegister::TError::ReadError);
            }
        }

        for (const auto& reg: registerRange->RegisterList()) {
            ScheduleNextPoll(reg, currentTime);
        }

        Device->SetLastReadTime(currentTime + sessionTime.GetSpentTime());
    }

    return registerRange;
}

std::list<PRegister> TPollableDevice::MarkWaitingRegistersAsReadErrorAndReschedule(
    std::chrono::steady_clock::time_point currentTime)
{
    std::list<PRegister> res;
    while (Registers.HasReadyItems(currentTime)) {
        res.push_back(Registers.GetTop().Data);
        Registers.Pop();
    }
    for (const auto& reg: res) {
        ScheduleNextPoll(reg, currentTime);
    }
    return res;
}

std::chrono::steady_clock::time_point TPollableDevice::GetDeadline() const
{
    if (Priority == TPriority::Low && Device->DeviceConfig()->MinRequestInterval != std::chrono::milliseconds::zero()) {
        auto deadline = Registers.GetDeadline();
        auto minNextReadTime = Device->GetLastReadTime() + Device->DeviceConfig()->MinRequestInterval;
        return minNextReadTime > deadline ? minNextReadTime : deadline;
    }
    return Registers.GetDeadline();
}

TPriority TPollableDevice::GetPriority() const
{
    return Priority;
}

PSerialDevice TPollableDevice::GetDevice() const
{
    return Device;
}

bool TPollableDevice::HasRegisters() const
{
    return !Registers.IsEmpty();
}

void TPollableDevice::RescheduleAllRegisters()
{
    for (const auto& reg: Device->GetRegisters()) {
        if ((Priority == TPriority::High && reg->IsHighPriority()) ||
            (Priority == TPriority::Low && !reg->IsHighPriority()))
        {
            if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
                if (reg->IsExcludedFromPolling() && !Registers.Contains(reg)) {
                    reg->SetAvailable(TRegisterAvailability::UNKNOWN);
                    reg->IncludeInPolling();
                    Registers.AddEntry(reg, std::chrono::steady_clock::time_point());
                }
            }
        }
    }
}

void TPollableDevice::ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point currentTime)
{
    if (reg->IsExcludedFromPolling()) {
        // If register is sporadic it must be read once to get actual value
        // Keep polling it until successful read
        if (reg->GetValue().GetType() == TRegisterValue::ValueType::Undefined) {
            Registers.AddEntry(reg, currentTime + std::chrono::microseconds(1));
        }
        return;
    }
    if (Priority == TPriority::High) {
        Registers.AddEntry(reg, currentTime + *(reg->ReadPeriod));
        return;
    }
    if (reg->ReadRateLimit) {
        Registers.AddEntry(reg, currentTime + *(reg->ReadRateLimit));
        return;
    }
    // Low priority registers should be scheduled to read as soon as possible,
    // but with a small delay after current read.
    Registers.AddEntry(reg, currentTime + std::chrono::microseconds(1));
}
