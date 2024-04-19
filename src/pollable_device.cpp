#include "pollable_device.h"

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Type != r2->Type) {
        return r1->Type > r2->Type;
    }
    auto cmp = r1->GetAddress().Compare(r2->GetAddress());
    if (cmp < 0) {
        return false;
    }
    if (cmp > 0) {
        return true;
    }
    // addresses are equal, compare offsets
    return r1->GetDataOffset() > r2->GetDataOffset();
}

bool TPollableDevice::AddRegister(PRegisterRange registerRange,
                                  PRegister reg,
                                  bool readAtLeastOneRegister,
                                  TItemAccumulationPolicy policy,
                                  std::chrono::milliseconds pollLimit,
                                  std::chrono::milliseconds maxPollTime)
{
    if (readAtLeastOneRegister) {
        if (policy == TItemAccumulationPolicy::Force) {
            return registerRange->Add(reg, std::chrono::milliseconds::max());
        }
        return registerRange->Add(reg, pollLimit);
    }
    if (policy == TItemAccumulationPolicy::Force) {
        return registerRange->Add(reg, maxPollTime);
    }
    return registerRange->Add(reg, std::min(maxPollTime, pollLimit));
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

PRegisterRange TPollableDevice::ReadRegisterRange(TItemAccumulationPolicy policy,
                                                  std::chrono::milliseconds pollLimit,
                                                  bool readAtLeastOneRegister,
                                                  std::chrono::milliseconds maxPollTime,
                                                  std::chrono::steady_clock::time_point currentTime,
                                                  TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    auto registerRange = Device->CreateRegisterRange();
    while (Registers.HasReadyItems(currentTime)) {
        const auto& item = Registers.GetTop();
        if (!AddRegister(registerRange, item.Data, readAtLeastOneRegister, policy, pollLimit, maxPollTime)) {
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

        Device->SetLastReadTime(currentTime);
    }

    return registerRange;
}

std::list<PRegister> TPollableDevice::SetReadError(std::chrono::steady_clock::time_point currentTime)
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
    if (Device->DeviceConfig()->MinRequestInterval != std::chrono::milliseconds::zero()) {
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

void TPollableDevice::RescheduleAllRegisters(std::chrono::steady_clock::time_point currentTime)
{
    for (const auto& reg: Device->GetRegisters()) {
        if ((Priority == TPriority::High && reg->IsHighPriority()) ||
            (Priority == TPriority::Low && !reg->IsHighPriority()))
        {
            if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
                if (reg->IsExcludedFromPolling() && !Registers.Contains(reg)) {
                    reg->SetAvailable(TRegisterAvailability::UNKNOWN);
                    reg->IncludeInPolling();
                    Registers.AddEntry(reg, currentTime);
                }
            }
        }
    }
}

void TPollableDevice::ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point currentTime)
{
    if (reg->IsExcludedFromPolling()) {
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