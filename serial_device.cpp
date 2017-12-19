#include "serial_device.h"

#include <iostream>
#include <unistd.h>

TSerialDevice::TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : Delay(config->Delay)
    , SerialPort(port)
    , _DeviceConfig(config)
    , _Protocol(protocol)
    , LastSuccessfulRead()
    , IsDisconnected(false)
{}

TSerialDevice::~TSerialDevice()
{
    /* TSerialDeviceFactory::RemoveDevice(shared_from_this()); */
}

std::string TSerialDevice::ToString() const
{
    return DeviceConfig()->Name + "(" + DeviceConfig()->SlaveId + ")";
}

std::list<PRegisterRange> TSerialDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    std::list<PRegisterRange> r;
    for (auto reg: reg_list)
        r.push_back(std::make_shared<TSimpleRegisterRange>(reg));
    return r;
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

void TSerialDevice::OnSuccessfulRead()
{
	LastSuccessfulRead = std::chrono::steady_clock::now();
	IsDisconnected = false;
}

void TSerialDevice::OnFailedRead()
{
	if (LastSuccessfulRead == std::chrono::steady_clock::time_point()) {
		LastSuccessfulRead = std::chrono::steady_clock::now();
	}

	if (std::chrono::steady_clock::now() - LastSuccessfulRead > _DeviceConfig->DeviceTimeout) {
		IsDisconnected = true;
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

bool TSerialDevice::WriteSetupRegisters()
{
    bool did_write = false;
    for (const auto& setup_item : SetupItems) {
        try {
        	std::cerr << "Init: " << setup_item->Name << ": setup register " <<
        			setup_item->Register->ToString() << " <-- " << setup_item->Value << std::endl;
            WriteRegister(setup_item->Register, setup_item->Value);
            did_write = true;
        } catch (const TSerialDeviceException& e) {
            std::cerr << "WARNING: device '" << setup_item->Register->Device()->ToString() <<
                "' register '" << setup_item->Register->ToString() <<
                "' setup failed: " << e.what() << std::endl;
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
