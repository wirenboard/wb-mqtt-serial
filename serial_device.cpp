#include "serial_device.h"
#include "ir_device_query_factory.h"
#include "ir_device_query.h"
#include "virtual_register.h"
#include "protocol_register_factory.h"
#include "protocol_register.h"

#include <iostream>
#include <unistd.h>
#include <cassert>


TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
{
    const auto & protocolRegisters = TProtocolRegisterFactory::GenerateProtocolRegisters(config->RegisterConfig, device);

    auto queries = TIRDeviceQueryFactory::GenerateQueries({ GetKeysAsSet(protocolRegisters) }, EQueryOperation::Write);
    assert(queries.size() == 1);

    Query = std::dynamic_pointer_cast<TIRDeviceValueQuery>(*queries.begin());
    assert(Query);

    TVirtualRegister::MapValueTo(Query, protocolRegisters, Value);
}

bool TProtocolInfo::IsSingleBitType(int) const
{
    return false;
}

int TProtocolInfo::GetMaxReadRegisters() const
{
    return 1;
}

int TProtocolInfo::GetMaxReadBits() const
{
    return 1;
}

int TProtocolInfo::GetMaxWriteRegisters() const
{
    return 1;
}

int TProtocolInfo::GetMaxWriteBits() const
{
    return 1;
}


TSerialDevice::TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : Delay(config->Delay)
    , SerialPort(port)
    , _DeviceConfig(config)
    , _Protocol(protocol)
    , LastSuccessfulCycle()
    , IsDisconnected(false)
    , RemainingFailCycles(config->DeviceMaxFailCycles)
{}

TSerialDevice::~TSerialDevice()
{
    /* TSerialDeviceFactory::RemoveDevice(shared_from_this()); */
}

const TProtocolInfo & TSerialDevice::GetProtocolInfo() const
{
    static TProtocolInfo info;
    return info;
}

std::string TSerialDevice::ToString() const
{
    return DeviceConfig()->Name + "(" + DeviceConfig()->SlaveId + ")";
}

void TSerialDevice::Execute(const PIRDeviceQuery & query)
{
    assert(query);

    try {
        try {
            switch(query->Operation) {
                case EQueryOperation::Read:
                    return Read(*query);

                case EQueryOperation::Write:
                    return Write(query->As<TIRDeviceValueQuery>());

                default:
                    throw TSerialDeviceException("unsupported query operation");
            }

            if (!query->IsExecuted()) {
                throw TSerialDeviceUnknownErrorException("query was not executed");
            }

        } catch (const std::exception & e) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "TSerialDevice::Execute(): failed to execute " << query->DescribeOperation() << " query " << query->Describe() << " of device " << ToString() << ": " << e.what() << std::endl;
            std::cerr.flags(f);
            throw;
        }
    } catch (const TSerialDeviceTransientErrorException & e) {
        query->SetStatus(EQueryStatus::DeviceTransientError);
    } catch (const TSerialDevicePermanentErrorException & e) {
        query->SetStatus(EQueryStatus::DevicePermanentError);
    } catch (const TSerialDeviceUnknownErrorException & e) {
        query->SetStatus(EQueryStatus::UnknownError);
    }

    assert(query->IsExecuted());
}

PProtocolRegister TSerialDevice::GetCreateRegister(uint32_t address, uint32_t type)
{
    PProtocolRegister protocolRegister(new TProtocolRegister(address, type, shared_from_this()));

    const auto & insRes = Registers.insert(protocolRegister);

    if (insRes.second) {
        if (Global::Debug) {
            std::cerr << "device " << ToString() << ": create register at " << address << std::endl;
        }
    } else {
        protocolRegister = *insRes.first;
    }

    return protocolRegister;
}

TPSetView<PProtocolRegister> TSerialDevice::CreateRegisterSetView(const PProtocolRegister & first, const PProtocolRegister & last) const
{
    assert(!Registers.empty());

    auto itFirst = Registers.find(first);
    auto itLast  = Registers.find(last);

    assert(itFirst != Registers.end());
    assert(itLast  != Registers.end());

    return {itFirst, itLast};
}

TPSetView<PProtocolRegister> TSerialDevice::StaticCreateRegisterSetView(const PProtocolRegister & first, const PProtocolRegister & last)
{
    auto device = first->GetDevice();

    assert(device);

    return device->CreateRegisterSetView(first, last);
}

void TSerialDevice::Prepare()
{
    Port()->Sleep(Delay);
}

void TSerialDevice::SleepGuardInterval() const
{
    if (DeviceConfig()->GuardInterval.count()) {
        Port()->Sleep(DeviceConfig()->GuardInterval);
    }
}

void TSerialDevice::Read(const TIRDeviceQuery & query)
{
    assert(query.GetCount() == 1);

    const auto & reg = query.RegView.GetFirst();

    SleepGuardInterval();

    query.FinalizeRead(ReadProtocolRegister(reg));
}

void TSerialDevice::Write(const TIRDeviceValueQuery & query)
{
    assert(query.GetCount() == 1);

    const auto & reg = query.RegView.GetFirst();
    uint64_t value;
    query.GetValues<uint64_t>(&value);

    SleepGuardInterval();

    WriteProtocolRegister(reg, value);
    query.FinalizeWrite();
}

uint64_t TSerialDevice::ReadProtocolRegister(const PProtocolRegister & reg)
{
    throw TSerialDeviceException("ReadProtocolRegister is not implemented");
}

void TSerialDevice::WriteProtocolRegister(const PProtocolRegister & reg, uint64_t value)
{
    throw TSerialDeviceException("WriteProtocolRegister is not implemented");
}

void TSerialDevice::EndPollCycle() {}

void TSerialDevice::OnCycleEnd(bool ok)
{
    // disable reconnect functionality option
    if (_DeviceConfig->DeviceTimeout.count() < 0 || _DeviceConfig->DeviceMaxFailCycles < 0) {
        return;
    }

    if (ok) {
        LastSuccessfulCycle = std::chrono::steady_clock::now();
        IsDisconnected = false;
        RemainingFailCycles = _DeviceConfig->DeviceMaxFailCycles;
    } else {
        if (LastSuccessfulCycle == std::chrono::steady_clock::time_point()) {
            LastSuccessfulCycle = std::chrono::steady_clock::now();
        }

        if (RemainingFailCycles > 0) {
            --RemainingFailCycles;
        }

        if ((std::chrono::steady_clock::now() - LastSuccessfulCycle > _DeviceConfig->DeviceTimeout) &&
            RemainingFailCycles == 0)
        {
            if (!IsDisconnected) {
                IsDisconnected = true;
                std::cerr << "device " << ToString() << " disconnected" << std::endl;
            }
        }
    }
}

bool TSerialDevice::GetIsDisconnected() const
{
	return IsDisconnected;
}

void TSerialDevice::InitSetupItems()
{
	for (auto& setup_item_config: _DeviceConfig->SetupItemConfigs) {
		SetupItems.push_back(std::make_shared<TDeviceSetupItem>(shared_from_this(), setup_item_config));
	}
}

bool TSerialDevice::HasSetupItems() const
{
    return !SetupItems.empty();
}

bool TSerialDevice::WriteSetupRegisters(bool tryAll)
{
    bool did_write = false;
    for (const auto & setupItem : SetupItems) {
        std::cerr << "Init: " << setupItem->Name << ": setup register " <<
                setupItem->Query->Describe() << " <-- " << setupItem->Value << std::endl;
        Execute(setupItem->Query);

        bool ok = setupItem->Query->GetStatus() == EQueryStatus::Ok;

        setupItem->Query->ResetStatus();

        if (ok) {
            did_write = true;
        } else {
            std::cerr << "WARNING: device '" << setupItem->Query->GetDevice()->ToString() <<
                "' registers '" << setupItem->Query->Describe() <<
                "' setup failed" << std::endl;
            if (!did_write && !tryAll) {
                break;
            }
        }
    }

    return did_write;
}

std::unordered_map<std::string, PProtocol>
    *TSerialDeviceFactory::Protocols = 0;

void TSerialDeviceFactory::RegisterProtocol(PProtocol protocol)
{
    if (!Protocols)
        Protocols = new std::unordered_map<std::string, PProtocol>();

    Protocols->insert(std::make_pair(protocol->GetName(), protocol));
}

const PProtocol TSerialDeviceFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    return TSerialDeviceFactory::GetProtocol(device_config->Protocol);
}

PProtocol TSerialDeviceFactory::GetProtocol(const std::string &name)
{
    auto it = Protocols->find(name);
    if (it == Protocols->end())
        throw TSerialDeviceException("unknown serial protocol: " + name);
    return it->second;
}

PSerialDevice TSerialDeviceFactory::CreateDevice(PDeviceConfig device_config, PPort port)
{
    return GetProtocolEntry(device_config)->CreateDevice(device_config, port);
}

void TSerialDeviceFactory::RemoveDevice(PSerialDevice device)
{
    if (device) {
        device->Protocol()->RemoveDevice(device);
    } else {
        throw TSerialDeviceException("can't remove empty device");
    }
}

PSerialDevice TSerialDeviceFactory::GetDevice(const std::string& slave_id, const std::string& protocol_name, PPort port)
{
    return GetProtocol(protocol_name)->GetDevice(slave_id, port);
}

PRegisterTypeMap TSerialDeviceFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config)->GetRegTypes();
}

template<>
int TBasicProtocolConverter<int>::ConvertSlaveId(const std::string &s) const
{
    try {
        return std::stoi(s, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + s + "\" is not convertible to string");
    }
}

template<>
TAggregatedSlaveId TBasicProtocolConverter<TAggregatedSlaveId>::ConvertSlaveId(const std::string &s) const
{
    try {
        auto delimiter_it = s.find(':');
        return {std::stoi(s.substr(0, delimiter_it), 0, 0), std::stoi(s.substr(delimiter_it + 1), 0, 0)};
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + s + "\" is not convertible to string");
    }
}
