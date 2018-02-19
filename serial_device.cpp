#include "serial_device.h"
#include "ir_device_query.h"
#include "virtual_register.h"

#include <iostream>
#include <unistd.h>


TDeviceSetupItem::TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
    : TDeviceSetupItemConfig(*config)
{
    TPSet<TProtocolRegister> protocolRegistersSet;
    {
        const auto & protocolRegisters = TProtocolRegister::GenerateProtocolRegisters(config->RegisterConfig, device);

        std::transform(protocolRegisters.begin(), protocolRegisters.end(),
            std::inserter(protocolRegistersSet, protocolRegistersSet.end()),
            [](const std::pair<PProtocolRegister, TRegisterBindInfo> & item) {
                return item.first;
            }
        );

        TVirtualRegister::MapValueTo(protocolRegisters, Value);
    }

    QuerySet = std::make_shared<TIRDeviceQuerySet>(protocolRegistersSet);
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


void TSerialDevice::Prepare()
{
    Port()->Sleep(Delay);
}

void TSerialDevice::EndPollCycle() {}

void TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    PSimpleRegisterRange simple_range = std::dynamic_pointer_cast<TSimpleRegisterRange>(range);
    if (!simple_range)
        throw std::runtime_error("simple range expected");
    simple_range->Reset();
    for (auto reg: simple_range->RegisterList()) {
        if (UnavailableAddresses.count(reg->Address)) {
        	continue;
        }
    	try {
            if (DeviceConfig()->GuardInterval.count()){
                Port()->Sleep(DeviceConfig()->GuardInterval);
            }
            simple_range->SetValue(reg, ReadRegister(reg));
        } catch (const TSerialDeviceTransientErrorException& e) {
            simple_range->SetError(reg);
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "TSerialDevice::ReadRegisterRange(): warning: " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "]" << std::endl;
            std::cerr.flags(f);
        } catch (const TSerialDevicePermanentRegisterException& e) {
        	UnavailableAddresses.insert(reg->Address);
        	simple_range->SetError(reg);
			std::ios::fmtflags f(std::cerr.flags());
			std::cerr << "TSerialDevice::ReadRegisterRange(): warning: " << e.what() << " [slave_id is "
					  << reg->Device()->ToString() + "] Register " << reg->ToString() << " is now counts as unsupported" << std::endl;
			std::cerr.flags(f);
        }
    }
}

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
            IsDisconnected = true;
            std::cerr << "device " << ToString() << " disconnected" << std::endl;
        }
    }
}

bool TSerialDevice::GetIsDisconnected() const
{
	return IsDisconnected;
}

void TSerialDevice::ResetUnavailableAddresses() {
	UnavailableAddresses.clear();
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
    for (const auto& setup_item : SetupItems) {
        for (const auto & query: setup_item->QuerySet->Queries) {
            try {
                std::cerr << "Init: " << setup_item->Name << ": setup register " <<
                        query->Describe() << " <-- " << setup_item->Value << std::endl;
                Write(query);
                did_write = true;
            } catch (const TSerialDeviceException& e) {
                std::cerr << "WARNING: device '" << setup_item->QuerySet->GetDevice()->ToString() <<
                    "' registers '" << setup_item->QuerySet->Describe() <<
                    "' setup failed: " << e.what() << std::endl;
                if (!did_write && !tryAll) {
                    break;
                }
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
