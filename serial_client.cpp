#include <unistd.h>
#include <unordered_map>
#include <iostream>

#include "serial_client.h"

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

TSerialClient::TSerialClient(PAbstractSerialPort port)
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

void TSerialClient::AddRegister(PRegister reg)
{
    if (Active)
        throw TSerialDeviceException("can't add registers to the active client");
    if (Handlers.find(reg) != Handlers.end())
        throw TSerialDeviceException("duplicate register");
    Handlers[reg] = std::make_shared<TRegisterHandler>(reg->Device(), reg, FlushNeeded, Debug);
    RegList.push_back(reg);
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

void TSerialClient::MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state)
{
    if (state != TRegisterHandler::UnknownErrorState && state != TRegisterHandler::ErrorStateUnchanged)
        ErrorCallback(reg, state);
}

bool TSerialClient::DoFlush()
{
    bool need_flush = false;
    for (const auto& reg: RegList) {
        auto handler = Handlers[reg];
        if (!handler->NeedToFlush())
            continue;
        PrepareToAccessDevice(handler->Device());
        MaybeUpdateErrorState(reg, handler->Flush());
        need_flush |= handler->NeedToFlush(); // register might still need to flush due to retry on fail functionality
    }
    return need_flush;
}

bool TSerialClient::WaitForPollAndFlush()
{
    bool need_flush = false;
    // When it's time for a next poll, take measures
    // to avoid poll starvation
    if (Plan->PollIsDue()) {
        return MaybeFlushAvoidingPollStarvationButDontWait();
    }
    auto wait_until = Plan->GetNextPollTimePoint();
    while (Port->Wait(FlushNeeded, wait_until)) {
        // Don't hold the lock while flushing
        need_flush |= DoFlush();
        if (Plan->PollIsDue()) {
            return MaybeFlushAvoidingPollStarvationButDontWait();
        }
    }
    return need_flush;
}

bool TSerialClient::MaybeFlushAvoidingPollStarvationButDontWait()
{
    bool need_flush = false;
    // avoid poll starvation
    int flush_count_remaining = MAX_FLUSHES_WHEN_POLL_IS_DUE;
    while (flush_count_remaining-- && FlushNeeded->TryWait()) {
        need_flush |= DoFlush();
    }
    return need_flush;
}

void TSerialClient::PollRange(PRegisterRange range)
{
    PSerialDevice dev = range->Device();
    PrepareToAccessDevice(dev);
    dev->ReadRegisterRange(range);
    bool all_failed = true;
    range->MapRange([this, &all_failed](PRegister reg, uint64_t new_value) {
    		all_failed = false;
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
        }, [this](PRegister reg) {
            bool changed;
            auto handler = Handlers[reg];
            if (handler->NeedToPoll())
                // TBD: separate AcceptDeviceReadError method (changed is unused here)
                MaybeUpdateErrorState(reg, handler->AcceptDeviceValue(0, false, &changed));
        });
    if (all_failed) {
    	dev->OnFailedRead();
    }
    else {
    	if (dev->GetIsDisconnected()) {
    		OnDeviceReconnect(dev);
    	}
    	dev->OnSuccessfulRead();
    }
}

void TSerialClient::Cycle()
{
    Connect();

    bool need_flush = WaitForPollAndFlush();
    Plan->ProcessPending([this, &need_flush](const PPollEntry& entry) {
            for (auto range: std::dynamic_pointer_cast<TSerialPollEntry>(entry)->Ranges)
                PollRange(range);
            need_flush |= MaybeFlushAvoidingPollStarvationButDontWait();
        });

    for (const auto& p: DevicesList)
        p->EndPollCycle();

    if (need_flush) {
        NotifyFlushNeeded();
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
	WriteSetupRegisters(dev);
}
