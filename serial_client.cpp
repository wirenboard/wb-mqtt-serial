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

TSerialClient::TSerialClient(PPort port)
    : Port(port),
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

PSerialDevice TSerialClient::CreateDevice(PDeviceConfig device_config)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    LOG(Debug) << "CreateDevice: " << device_config->Id <<
            (device_config->DeviceType.empty() ? "" : " (" + device_config->DeviceType + ")") <<
            " @ " << device_config->SlaveId << " -- protocol: " << device_config->Protocol;

    try {
        PSerialDevice dev = TSerialDeviceFactory::CreateDevice(device_config, Port);
        DevicesList.push_back(dev);
        return dev;
    } catch (const TSerialDeviceException& e) {
        Disconnect();
        throw;
    }
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
    if (!Port->IsOpen())
        Port->Open();
    PrepareRegisterRanges();
    Active = true;
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
                    return a->Type < b->Type || (a->Type == b->Type && a->Address < b->Address);
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

void TSerialClient::SplitRegisterRanges(std::set<PRegisterRange> && ranges)
{
    if (ranges.empty()) {
        return;
    }

    Plan->Modify([&](const PPollEntry & entry){
        if (auto serial_entry = std::dynamic_pointer_cast<TSerialPollEntry>(entry)) {
            for (auto itRange = serial_entry->Ranges.begin(); itRange != serial_entry->Ranges.end();) {
                if (ranges.count(*itRange)) {
                    auto device = (*itRange)->Device();

                    std::cerr << "Disabling holes feature for register range" << std::endl;

                    auto newRanges = device->SplitRegisterList((*itRange)->RegisterList(), false);
                    serial_entry->Ranges.insert(itRange, newRanges.begin(), newRanges.end());

                    ranges.erase(*itRange);
                    itRange = serial_entry->Ranges.erase(itRange);
                } else {
                    ++itRange;
                }
            }
        }

        return ranges.empty();
    });
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

void TSerialClient::PollRange(PRegisterRange range)
{
    PSerialDevice dev = range->Device();
    PrepareToAccessDevice(dev);
    dev->ReadRegisterRange(range);
    range->MapRange([this, &range](PRegister reg, uint64_t new_value) {
            bool changed;
            auto handler = Handlers[reg];

            if (handler->NeedToPoll()) {
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(new_value, true, &changed));
                // Note that handler->CurrentErrorState() is not the
                // same as the value returned by handler->AcceptDeviceValue(...),
                // because the latter may be ErrorStateUnchanged.
                if (handler->CurrentErrorState() != TRegisterHandler::ReadError &&
                    handler->CurrentErrorState() != TRegisterHandler::ReadWriteError)
                    ReadCallback(reg, changed);
            }
        }, [this, &range](PRegister reg) {
            bool changed;
            auto handler = Handlers[reg];

            if (handler->NeedToPoll())
                // TBD: separate AcceptDeviceReadError method (changed is unused here)
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
        });
}

void TSerialClient::Cycle()
{
    Connect();

    Port->CycleBegin();

    WaitForPollAndFlush();

    // devices whose registers were polled during this cycle and statues
    std::map<PSerialDevice, std::set<TRegisterRange::EStatus>> devicesRangesStatuses;
    // ranges that needs to split
    std::set<PRegisterRange> rangesToSplit;

    Plan->ProcessPending([&](const PPollEntry& entry) {
        for (auto range: std::dynamic_pointer_cast<TSerialPollEntry>(entry)->Ranges) {
            auto device = range->Device();
            auto & statuses = devicesRangesStatuses[device];

            if (device->GetIsDisconnected()) {
                // limited polling mode
                if (statuses.empty()) {
                    // First interaction with disconnected device within this cycle: Try to reconnect
                    
                    // Force Prepare() (i.e. start session)
                    try {
                        device->Prepare();
                    } catch ( const TSerialDeviceTransientErrorException& e) {
                        std::cerr << "TSerialDevice::Prepare(): warning: " << e.what() << " [slave_id is "
                            << device->ToString() + "]" << std::endl;
                    }

                    if (device->HasSetupItems()) {
                        auto wrote = device->WriteSetupRegisters(false);
                        statuses.insert(wrote ? TRegisterRange::ST_OK : TRegisterRange::ST_UNKNOWN_ERROR);
                        if (!wrote) {
                            continue;
                        }
                    }
                } else {
                    // Not first interaction with disconnected device that has only errors - still disconnected
                    if (statuses.count(TRegisterRange::ST_UNKNOWN_ERROR) == statuses.size()) {
                        continue;
                    }
                }
            }

            PollRange(range);
            statuses.insert(range->GetStatus());
            if (range->NeedsSplit()) {
                rangesToSplit.insert(range);
            }
        }
        MaybeFlushAvoidingPollStarvationButDontWait();
    });

    for (const auto & deviceRangesStatuses: devicesRangesStatuses) {
        const auto & device = deviceRangesStatuses.first;
        const auto & statuses = deviceRangesStatuses.second;

        if (statuses.empty()) {
            std::cerr << "invariant violation: statuses empty @ " << __func__ << std::endl;
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

    SplitRegisterRanges(std::move(rangesToSplit));

    for (const auto& p: DevicesList) {
        p->EndPollCycle();
    }

    // Port status
    {
        bool cycleFailed = std::all_of(DevicesList.begin(), DevicesList.end(),
            [](const PSerialDevice & device){ return device->GetIsDisconnected(); }
        );

        Port->CycleEnd(!cycleFailed);
    }
}

bool TSerialClient::WriteSetupRegisters(PSerialDevice dev)
{
    Connect();
    PrepareToAccessDevice(dev);
    return dev->WriteSetupRegisters();
}

void TSerialClient::ClearDevices()
{
    for (auto &dev : DevicesList) {
        TSerialDeviceFactory::RemoveDevice(dev);
    }
    DevicesList.clear();
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
                std::cerr << "TSerialDevice::EndSession(): warning: " << e.what() << " [slave_id is "
                    << LastAccessedDevice->ToString() + "]" << std::endl;
            }
        }
        LastAccessedDevice = dev;
        try {
            dev->Prepare();
        } catch ( const TSerialDeviceTransientErrorException& e) {
            std::cerr << "TSerialDevice::Prepare(): warning: " << e.what() << " [slave_id is "
                << dev->ToString() + "]" << std::endl;
        }
    }
}

void TSerialClient::OnDeviceReconnect(PSerialDevice dev)
{
	LOG(Debug) << "device " << dev->ToString() << " reconnected";
	dev->ResetUnavailableAddresses();
}
