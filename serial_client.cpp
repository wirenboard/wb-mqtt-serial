#include "serial_client.h"
#include "virtual_register.h"
#include "ir_device_query.h"

#include <unistd.h>
#include <unordered_map>
#include <iostream>


namespace {
    struct TSerialPollEntry: public TPollEntry {
        PIRDeviceReadQuery ReadQuery;
        TIntervalMs        PollIntervalValue;

        TSerialPollEntry(TIntervalMs pollInterval, const PIRDeviceReadQuery & readQuery)
            : ReadQuery(readQuery)
            , PollIntervalValue(pollInterval)
        {}

        TIntervalMs PollInterval() const override {
            return PollIntervalValue;
        }
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
    for (auto &dev : DevicesList)
        TSerialDeviceFactory::RemoveDevice(dev);
}

PSerialDevice TSerialClient::CreateDevice(PDeviceConfig device_config)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Debug)
        std::cerr << "CreateDevice: " << device_config->Id <<
            (device_config->DeviceType.empty() ? "" : " (" + device_config->DeviceType + ")") <<
            " @ " << device_config->SlaveId << " -- protocol: " << device_config->Protocol << std::endl;

    try {
        PSerialDevice dev = TSerialDeviceFactory::CreateDevice(device_config, Port);
        DevicesList.push_back(dev);
        return dev;
    } catch (const TSerialDeviceException& e) {
        Disconnect();
        throw;
    }
}

void TSerialClient::AddRegister(PVirtualRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");

    bool inserted = VirtualRegisters.insert(reg).second;
    if (!inserted) {
        throw TSerialDeviceException("duplicate register");
    }

    {
        auto & deviceProtocolRegisters = ProtocolRegisters[reg->GetDevice()];
        auto & protocolRegisters = reg->GetProtocolRegisters();
        deviceProtocolRegisters.insert(protocolRegisters.begin(), protocolRegisters.end());
    }

    //auto handler = Handlers[reg] = std::make_shared<TRegisterHandler>(reg->GetDevice(), reg, FlushNeeded, Debug);

    if (Debug)
        std::cerr << "AddRegister: " << reg << std::endl;
}

void TSerialClient::Connect()
{
    if (Active)
        return;
    if (!Handlers.size())
        throw TSerialDeviceException("no registers defined");
    if (!Port->IsOpen())
        Port->Open();
    GenerateReadQueries();
    Active = true;
}

void TSerialClient::Disconnect()
{
    if (Port->IsOpen())
        Port->Close();
    Active = false;
}

void TSerialClient::GenerateReadQueries()
{
    // all of this is seemingly slow but it's actually only done once
    Plan->Reset();
    TProtocolRegisterSet curentRegisters;

    for (const auto & deviceProtocolRegisters: ProtocolRegisters) {
        std::unordered_map<TIntervalMs, TProtocolRegisterSet> protocolRegistersByInterval;

        const auto & device            = deviceProtocolRegisters.first;
        const auto & protocolRegisters = deviceProtocolRegisters.second;

        for (const auto & protocolRegister: protocolRegisters) {
            for (const auto & pollInterval: protocolRegister->GetPollIntervals()) {
                protocolRegistersByInterval[pollInterval].insert(protocolRegister);
            }
        }

        for (const auto & pollIntervalRegisters: protocolRegistersByInterval) {
            const auto & pollInterval = pollIntervalRegisters.first;
            const auto & registers    = pollIntervalRegisters.second;

            const auto & query = std::make_shared<TIRDeviceReadQuery>(registers);
            Plan->AddEntry(std::make_shared<TSerialPollEntry>(pollInterval, query));
        }
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
    for (const auto& reg: VirtualRegisters) {
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

    Plan->ProcessPending([&](const PPollEntry& entry) {
        const auto & query = std::dynamic_pointer_cast<TSerialPollEntry>(entry)->ReadQuery;
        auto device = query->Device;

        auto & statuses = devicesRangesStatuses[device];

        for (const auto & entry: query->Entries) {
            if (device->GetIsDisconnected()) {
                // limited polling mode
                if (statuses.empty()) {
                    // First interaction with disconnected device within this cycle: Try to reconnect
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
        }

        query->SplitIfNeeded();

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

void TSerialClient::SetDebug(bool debug)
{
    Debug = debug;
    Port->SetDebug(debug);
    for (const auto& p: Handlers)
        p.second->SetDebug(debug);
}

bool TSerialClient::DebugEnabled() const {
    return Debug;
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
        LastAccessedDevice = dev;
        dev->Prepare();
    }
}

void TSerialClient::OnDeviceReconnect(PSerialDevice dev)
{
	if (Debug) {
		std::cerr << "device " << dev->ToString() << " reconnected" << std::endl;
	}
	dev->ResetUnavailableAddresses();
}
