#include "serial_device.h"

#include <iostream>
#include <unistd.h>
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial device] "

TSerialDevice::TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : Delay(config->Delay)
    , SerialPort(port)
    , _DeviceConfig(config)
    , _Protocol(protocol)
    , LastSuccessfulCycle()
    , IsDisconnected(true)
    , RemainingFailCycles(config->DeviceMaxFailCycles)
{}

TSerialDevice::~TSerialDevice()
{
    /* TSerialDeviceFactory::RemoveDevice(shared_from_this()); */
}

std::string TSerialDevice::ToString() const
{
    return DeviceConfig()->Name + "(" + DeviceConfig()->SlaveId + ")";
}

std::list<PRegisterRange> TSerialDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool) const
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

void TSerialDevice::SetReadError(PRegisterRange range)
{
    PSimpleRegisterRange simple_range = std::dynamic_pointer_cast<TSimpleRegisterRange>(range);
    if (!simple_range) {
        throw std::runtime_error("simple range expected");
    }
    simple_range->Reset();
    for (auto reg: simple_range->RegisterList()) {
        simple_range->SetError(reg);
    }
}

std::list<PRegisterRange> TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    PSimpleRegisterRange simple_range = std::dynamic_pointer_cast<TSimpleRegisterRange>(range);
    if (!simple_range)
        throw std::runtime_error("simple range expected");
    simple_range->Reset();
    for (auto reg: simple_range->RegisterList()) {
        if (!reg->IsAvailable()) {
            continue;
        }
        try {
            if (DeviceConfig()->GuardInterval.count()){
                Port()->Sleep(DeviceConfig()->GuardInterval);
            }
            simple_range->SetValue(reg, ReadRegister(reg));
        } catch (const TSerialDeviceTransientErrorException& e) {
            simple_range->SetError(reg);
            LOG(Warn) << "TSerialDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "]";
        } catch (const TSerialDevicePermanentRegisterException& e) {
            reg->SetAvailable(false);
            simple_range->SetError(reg);
            LOG(Warn) << "TSerialDevice::ReadRegisterRange(): warning: " << e.what() << " [slave_id is "
                  << reg->Device()->ToString() + "] Register " << reg->ToString() << " is now counts as unsupported";
        }
    }
    return std::list<PRegisterRange>{range};
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
            LOG(Info) << "device " << ToString() << " disconnected";
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

bool TSerialDevice::WriteSetupRegisters()
{
    for (const auto& setup_item : SetupItems) {
        try {
        	LOG(Info) << "Init: " << setup_item->Name 
                      << ": setup register " << setup_item->Register->ToString()
                      << " <-- " << setup_item->Value;
            WriteRegister(setup_item->Register, setup_item->Value);
        } catch (const TSerialDeviceException & e) {
            LOG(Warn) << "Register '" << setup_item->Register->ToString()
                      << "' setup failed: " << e.what();
            return false;
        }
    }
    return true;
}

std::unordered_map<std::string, PProtocol> * TSerialDeviceFactory::Protocols = nullptr;

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
unsigned long TBasicProtocolConverter<unsigned long>::ConvertSlaveId(const std::string &s) const
{
    try {
        return std::stoul(s, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + s + "\" is not convertible to string");
    }
}

template<>
std::string TBasicProtocolConverter<std::string>::ConvertSlaveId(const std::string &s) const
{
    return s;
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
