#include "serial_client.h"
#include "log.h"

#include <unistd.h>
#include <unordered_map>
#include <iostream>

#define LOG(logger) ::logger.Log() << "[serial client] "

namespace {
    struct TSerialPollEntry: public TPollEntry {
        TSerialPollEntry(PRegisterRange range) {
            Ranges.push_back(range);
        }
        std::chrono::milliseconds PollInterval() const {
            return Ranges.front()->PollInterval();
        }
        std::list<PRegisterRange> Ranges;
    };
    typedef std::shared_ptr<TSerialPollEntry> PSerialPollEntry;
};

TSerialClient::TSerialClient(const std::vector<PSerialDevice>& devices, PPort port)
    : Port(port),
      Devices(devices),
      Active(false),
      ReadCallback([](PRegister, bool){}),
      ErrorCallback([](PRegister, bool){}),
      FlushNeeded(new TBinarySemaphore),
      Plan(std::make_shared<TPollPlan>([this]() { return Port->CurrentTime(); })) {}

TSerialClient::~TSerialClient()
{
    if (Active)
        Disconnect();

    // remove all registered devices
    ClearDevices();
}

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg, FlushNeeded);
    RegList.push_back(reg);
    LOG(Debug) << "AddRegister: " << reg;
}

void TSerialClient::Connect()
{
    if (Active)
        return;
    if (!Handlers.size())
        throw TSerialDeviceException("no registers defined");
    try {
        Port->Open();
        Active = true;
    } catch (const TSerialDeviceException& e) {
        LOG(Warn) << e.what();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    PrepareRegisterRanges();
}

void TSerialClient::Disconnect()
{
    if (Port->IsOpen())
        Port->Close();
    Active = false;
}

void TSerialClient::PrepareRegisterRanges()
{
    // all of this is seemingly slow but it's actually only done once
    Plan->Reset();
    PSerialDevice last_device(0);
    std::list<PRegister> cur_regs;
    auto it = RegList.begin();
    std::list<PSerialPollEntry> entries;
    std::unordered_map<long long, PSerialPollEntry> interval_map;
    for (;;) {
        bool at_end = it == RegList.end();
        if ((at_end || (*it)->Device() != last_device) && !cur_regs.empty()) {
            cur_regs.sort([](const PRegister& a, const PRegister& b) {
                    return a->Type < b->Type || (a->Type == b->Type && a->Address->IsLessThan(*b->Address));
                });
            interval_map.clear();

            // Join multiple ranges with same poll period into a
            // single scheduling entry. This is necessary because
            // switching between devices may require extra
            // delays. This is far from being an ideal solution
            // though.
            for (auto range: last_device->SplitRegisterList(cur_regs)) {
                PSerialPollEntry entry;
                long long interval = range->PollInterval().count();
                auto it = interval_map.find(interval);
                if (it == interval_map.end()) {
                    entry = std::make_shared<TSerialPollEntry>(range);
                    interval_map[interval] = entry;
                    Plan->AddEntry(entry);
                } else
                    it->second->Ranges.push_back(range);
            }
            cur_regs.clear();
        }
        if (at_end)
            break;
        last_device = (*it)->Device();
        cur_regs.push_back(*it++);
    }
}

void TSerialClient::MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state)
{
    if (state != TRegisterHandler::UnknownErrorState && state != TRegisterHandler::ErrorStateUnchanged)
        ErrorCallback(reg, state);
}

void TSerialClient::DoFlush()
{
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        PrepareToAccessDevice(handler->Device());
        MaybeUpdateErrorState(reg, handler->Flush());
    }
}

void TSerialClient::WaitForPollAndFlush()
{
    // When it's time for a next poll, take measures
    // to avoid poll starvation
    if (Plan->PollIsDue()) {
        MaybeFlushAvoidingPollStarvationButDontWait();
        return;
    }
    auto wait_until = Plan->GetNextPollTimePoint();
    while (Port->Wait(FlushNeeded, wait_until)) {
        // Don't hold the lock while flushing
        DoFlush();
        if (Plan->PollIsDue()) {
            MaybeFlushAvoidingPollStarvationButDontWait();
            return;
        }
    }
}

void TSerialClient::MaybeFlushAvoidingPollStarvationButDontWait()
{
    // avoid poll starvation
    int flush_count_remaining = MAX_FLUSHES_WHEN_POLL_IS_DUE;
    while (flush_count_remaining-- && FlushNeeded->TryWait())
        DoFlush();
}

void TSerialClient::SetReadError(PRegisterRange range)
{
    for (auto reg: range->RegisterList()) {
        reg->SetError();
        bool changed;
        auto handler = Handlers[reg];

        if (handler->NeedToPoll())
            // TBD: separate AcceptDeviceReadError method (changed is unused here)
            MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
    }
}

std::list<PRegisterRange> TSerialClient::PollRange(PRegisterRange range)
{
    PSerialDevice dev = range->Device();
    PrepareToAccessDevice(dev);
    std::list<PRegisterRange> newRanges = dev->ReadRegisterRange(range);
    for (auto& reg: range->RegisterList()) {
        bool changed;
        auto handler = Handlers[reg];
        if (reg->GetError()) {
            if (handler->NeedToPoll())
                // TBD: separate AcceptDeviceReadError method (changed is unused here)
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
        } else {
            if (handler->NeedToPoll()) {
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(reg->GetValue(), true, &changed));
                // Note that handler->CurrentErrorState() is not the
                // same as the value returned by handler->AcceptDeviceValue(...),
                // because the latter may be ErrorStateUnchanged.
                if (handler->CurrentErrorState() != TRegisterHandler::ReadError &&
                    handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
                    ReadCallback(reg, changed);
            }
        }
    }
    return newRanges;
}

void TSerialClient::Cycle()
{
    Connect();

    Port->CycleBegin();

    WaitForPollAndFlush();

    // devices whose registers were polled during this cycle and statues
    std::map<PSerialDevice, std::set<TRegisterRange::EStatus>> devicesRangesStatuses;

    Plan->ProcessPending([&](const PPollEntry& entry) {
        auto pollEntry = std::dynamic_pointer_cast<TSerialPollEntry>(entry);
        std::list<PRegisterRange> newRanges;
        for (auto range: pollEntry->Ranges) {
            auto device = range->Device();
            auto & statuses = devicesRangesStatuses[device];

            if (device->GetIsDisconnected()) {
                // limited polling mode
                if (statuses.empty()) {
                    // First interaction with disconnected device within this cycle: Try to reconnect
                    
                    // TODO: Not a good solution as LastAccessedDevice can be disconnected too.
                    //       But we can't rely on GetIsDisconnected here because it updates only after full cycle.
                    //       The whole EndSession/GetIsDisconnected logic should be revised
                    try {
                        if (LastAccessedDevice && LastAccessedDevice != device ) {
                            LastAccessedDevice->EndSession();
                        }
                    } catch ( const TSerialDeviceTransientErrorException& e) {
                        LOG(Warn) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is " << LastAccessedDevice->ToString() + "]";
                    }

                    // Force Prepare() (i.e. start session)
                    try {
                        LastAccessedDevice = device;
                        device->Prepare();
                    } catch ( const TSerialDeviceTransientErrorException& e) {
                        LOG(Warn) << "TSerialDevice::Prepare(): " << e.what() << " [slave_id is " << device->ToString() + "]";
                        statuses.insert(TRegisterRange::ST_UNKNOWN_ERROR);
                    }

                    if (device->HasSetupItems()) {
                        auto wrote = device->WriteSetupRegisters();
                        statuses.insert(wrote ? TRegisterRange::ST_OK : TRegisterRange::ST_UNKNOWN_ERROR);
                    }
                }
                // Interaction with disconnected device that has only errors - still disconnected
                if (!statuses.empty() && statuses.count(TRegisterRange::ST_UNKNOWN_ERROR) == statuses.size()) {
                    SetReadError(range);
                    newRanges.push_back(range);
                    continue;
                }
            }
            try {
                newRanges.splice(newRanges.end(), PollRange(range));
                statuses.insert(range->GetStatus());
            } catch (const TSerialDeviceException& e) {
                LOG(Error) << e.what();
                statuses.insert(TRegisterRange::ST_UNKNOWN_ERROR);
                SetReadError(range);
                newRanges.push_back(range);
            }
        }
        MaybeFlushAvoidingPollStarvationButDontWait();
        pollEntry->Ranges.swap(newRanges);
    });

    for (const auto & deviceRangesStatuses: devicesRangesStatuses) {
        const auto & device = deviceRangesStatuses.first;
        const auto & statuses = deviceRangesStatuses.second;

        if (statuses.empty()) {
            LOG(Debug) << "invariant violation: statuses empty @ " << __func__;
            continue;   // this should not happen
        }

        bool deviceWasDisconnected = device->GetIsDisconnected(); // don't move after device->OnCycleEnd(...);
        {
            bool cycleFailed = statuses.count(TRegisterRange::ST_UNKNOWN_ERROR) == statuses.size();
            device->OnCycleEnd(!cycleFailed);
        }

        if (deviceWasDisconnected && !device->GetIsDisconnected()) {
            OnDeviceReconnect(device);
        }
    }

    for (const auto& p: Devices) {
        p->EndPollCycle();
    }

    // Port status
    {
        bool cycleFailed = std::all_of(Devices.begin(), Devices.end(),
            [](const PSerialDevice & device){ return device->GetIsDisconnected(); }
        );

        Port->CycleEnd(!cycleFailed);
    }
}

void TSerialClient::ClearDevices()
{
    Devices.clear();
}

void TSerialClient::SetTextValue(PRegister reg, const std::string& value)
{
    GetHandler(reg)->SetTextValue(value);
}

std::string TSerialClient::GetTextValue(PRegister reg) const
{
    return GetHandler(reg)->TextValue();
}

bool TSerialClient::DidRead(PRegister reg) const
{
    return GetHandler(reg)->DidRead();
}

void TSerialClient::SetReadCallback(const TSerialClient::TReadCallback& callback)
{
    ReadCallback = callback;
}

void TSerialClient::SetErrorCallback(const TSerialClient::TErrorCallback& callback)
{
    ErrorCallback = callback;
}

void TSerialClient::NotifyFlushNeeded()
{
    FlushNeeded->Signal();
}

PRegisterHandler TSerialClient::GetHandler(PRegister reg) const
{
    auto it = Handlers.find(reg);
    if (it == Handlers.end())
        throw TSerialDeviceException("register not found");
    return it->second;
}

void TSerialClient::PrepareToAccessDevice(PSerialDevice dev)
{
    if (dev != LastAccessedDevice) {
        if (LastAccessedDevice) {
            try {
                LastAccessedDevice->EndSession();
            } catch ( const TSerialDeviceTransientErrorException& e) {
                LOG(Warn) << "TSerialDevice::EndSession(): " << e.what() << " [slave_id is " << LastAccessedDevice->ToString() + "]";
            }
        }
        LastAccessedDevice = dev;
        try {
            dev->Prepare();
        } catch ( const TSerialDeviceTransientErrorException& e) {
            LOG(Warn) << "TSerialDevice::Prepare(): warning: " << e.what() << " [slave_id is " << dev->ToString() + "]";
        }
    }
}

void TSerialClient::OnDeviceReconnect(PSerialDevice dev)
{
	LOG(Info) << "device " << dev->ToString() << " is connected";
    for(auto& reg: RegList) {
        if (reg->Device() == dev) {
            reg->SetAvailable(true);
        }
    }
}
