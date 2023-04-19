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
        PSerialDevice Device;
        TPriority Priority;

    public:
        TRegisterReader(milliseconds maxPollTime): MaxPollTime(maxPollTime)
        {}

        bool operator()(const PRegister& reg,
                        TItemAccumulationPolicy policy,
                        milliseconds pollLimit,
                        TPriority priority)
        {
            if (!Device) {
                RegisterRange = reg->Device()->CreateRegisterRange();
                Device = reg->Device();
                Priority = priority;
            }
            if (Device != reg->Device()) {
                return false;
            }
            if (policy == TItemAccumulationPolicy::Force) {
                return RegisterRange->Add(reg, milliseconds::max());
            }
            return RegisterRange->Add(reg, std::min(MaxPollTime, pollLimit));
        }

        PRegisterRange GetRegisterRange() const
        {
            return RegisterRange;
        }

        TPriority GetPriority() const
        {
            return Priority;
        }
    };

    class TClosedPortRegisterReader
    {
        std::list<PRegister> Regs;

    public:
        bool operator()(const PRegister& reg,
                        TItemAccumulationPolicy policy,
                        milliseconds pollLimit,
                        TPriority priority)
        {
            Regs.emplace_back(reg);
            return true;
        }

        std::list<PRegister>& GetRegisters()
        {
            return Regs;
        }

        void ClearRegisters()
        {
            Regs.clear();
        }
    };
};

TSerialClientRegisterPoller::TSerialClientRegisterPoller(size_t lowPriorityRateLimit)
    : Scheduler(MAX_LOW_PRIORITY_LAG, lowPriorityRateLimit),
      ThrottlingStateLogger()
{}

void TSerialClientRegisterPoller::PrepareRegisterRanges(const std::list<PRegister>& regList)
{
    RegList = regList;
    auto now = steady_clock::now();
    for (auto& reg: RegList) {
        if (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
            Scheduler.AddEntry(reg, now, reg->IsHighPriority() ? TPriority::High : TPriority::Low);
        }
    }
}

void TSerialClientRegisterPoller::SetReadError(PRegister reg)
{
    reg->SetError(TRegister::TError::ReadError);
    if (ErrorCallback) {
        ErrorCallback(reg);
    }
}

void TSerialClientRegisterPoller::ProcessPolledRegister(PRegister reg)
{
    if (reg->GetErrorState().test(TRegister::ReadError) || reg->GetErrorState().test(TRegister::WriteError)) {
        if (ErrorCallback) {
            ErrorCallback(reg);
        }
    } else {
        if (ReadCallback) {
            ReadCallback(reg);
        }
    }
}

void TSerialClientRegisterPoller::ScheduleNextPoll(PRegister reg, steady_clock::time_point pollStartTime)
{
    if (reg->IsExcludedFromPolling()) {
        return;
    }
    if (reg->IsHighPriority()) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadPeriod), TPriority::High);
        return;
    }
    if (reg->ReadRateLimit) {
        Scheduler.AddEntry(reg, pollStartTime + *(reg->ReadRateLimit), TPriority::Low);
        return;
    }
    // Low priority tasks should be scheduled to read as soon as possible,
    // but with a small delay after current read.
    Scheduler.AddEntry(reg, pollStartTime + 1us, TPriority::Low);
}

void TSerialClientRegisterPoller::ClosedPortCycle(steady_clock::time_point currentTime)
{
    Scheduler.ResetLoadBalancing();

    TClosedPortRegisterReader reader;
    do {
        reader.ClearRegisters();
        Scheduler.AccumulateNext(currentTime, reader);
        for (auto& reg: reader.GetRegisters()) {
            SetReadError(reg);
            ScheduleNextPoll(reg, currentTime);
            reg->Device()->SetTransferResult(false);
        }
    } while (!reader.GetRegisters().empty());
}

void TSerialClientRegisterPoller::SetReadCallback(TRegisterCallback callback)
{
    ReadCallback = std::move(callback);
}

void TSerialClientRegisterPoller::SetErrorCallback(TRegisterCallback callback)
{
    ErrorCallback = std::move(callback);
}

PSerialDevice TSerialClientRegisterPoller::OpenPortCycle(TPort& port,
                                                         std::chrono::steady_clock::time_point currentTime,
                                                         std::chrono::milliseconds maxPollingTime,
                                                         TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    TRegisterReader reader(maxPollingTime);

    auto throttlingState = Scheduler.AccumulateNext(currentTime, reader);
    auto throttlingMsg = ThrottlingStateLogger.GetMessage(throttlingState);
    if (!throttlingMsg.empty()) {
        LOG(Warn) << port.GetDescription() << " " << throttlingMsg;
    }
    auto range = reader.GetRegisterRange();
    if (!range) {
        // Nothing to read
        return nullptr;
    }
    if (range->RegisterList().empty()) {
        // There are registers waiting read but they don't fit in allowed poll limit
        // Wait for high priority registers

        // TODO: !!!
        return nullptr;
    }

    auto device = range->RegisterList().front()->Device();
    bool deviceWasConnected = !device->GetIsDisconnected();
    if (lastAccessedDevice.PrepareToAccess(device)) {
        device->ReadRegisterRange(range);
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(currentTime);
            ProcessPolledRegister(reg);
            ScheduleNextPoll(reg, currentTime);
        }
    } else {
        for (auto& reg: range->RegisterList()) {
            reg->SetLastPollTime(currentTime);
            ScheduleNextPoll(reg, currentTime);
            SetReadError(reg);
        }
    }

    if (deviceWasConnected && device->GetIsDisconnected()) {
        for (auto& reg: RegList) {
            if (reg->Device() == device) {
                bool wasExcludedFromPolling = reg->IsExcludedFromPolling();
                reg->SetAvailable(TRegisterAvailability::UNKNOWN);
                if (wasExcludedFromPolling) {
                    ScheduleNextPoll(reg, currentTime);
                }
            }
        }
    }

    Scheduler.UpdateSelectionTime(duration_cast<milliseconds>(steady_clock::now() - currentTime), reader.GetPriority());
    return device;
}

std::chrono::steady_clock::time_point TSerialClientRegisterPoller::GetDeadline(
    std::chrono::steady_clock::time_point currentTime) const
{
    return Scheduler.GetDeadline(currentTime);
}

bool TRegisterComparePredicate::operator()(const PRegister& r1, const PRegister& r2) const
{
    if (r1->Device() != r2->Device()) {
        return r1->Device()->DeviceConfig()->SlaveId > r2->Device()->DeviceConfig()->SlaveId;
    }
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

TThrottlingStateLogger::TThrottlingStateLogger(): FirstTime(true)
{}

std::string TThrottlingStateLogger::GetMessage(TThrottlingState state)
{
    if (FirstTime && state == TThrottlingState::LowPriorityRateLimit) {
        FirstTime = false;
        return "Register read rate limit is exceeded";
    }
    return std::string();
}
