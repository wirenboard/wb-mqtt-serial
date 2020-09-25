#include "serial_device.h"

#include <iostream>
#include <unistd.h>
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial device] "

IProtocol::IProtocol(const std::string& name, const TRegisterTypes& reg_types)
    : Name(name)
{
    RegTypes = std::make_shared<TRegisterTypeMap>();
    for (const auto& rt : reg_types)
        RegTypes->insert(std::make_pair(rt.Name, rt));
}

IProtocol::~IProtocol()
{}

const std::string& IProtocol::GetName() const
{ 
    return Name;
}

PRegisterTypeMap IProtocol::GetRegTypes() const
{
    return RegTypes;
}

TSerialDevice::TSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : SerialPort(port)
    , _DeviceConfig(config)
    , _Protocol(protocol)
    , LastSuccessfulCycle()
    , IsDisconnected(true)
    , RemainingFailCycles(config->DeviceMaxFailCycles)
{}

TSerialDevice::~TSerialDevice()
{}

std::string TSerialDevice::ToString() const
{
    return Protocol()->GetName() + ":" + DeviceConfig()->SlaveId;
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
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
}

void TSerialDevice::EndPollCycle() {}


uint64_t TSerialDevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("single register reading is not supported");
}

std::list<PRegisterRange> TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    PSimpleRegisterRange simple_range = std::dynamic_pointer_cast<TSimpleRegisterRange>(range);
    if (!simple_range)
        throw std::runtime_error("simple range expected");
    for (auto reg: simple_range->RegisterList()) {
        if (!reg->IsAvailable()) {
            continue;
        }
    	try {
            Port()->SleepSinceLastInteraction(DeviceConfig()->RequestDelay);
            reg->SetValue(ReadRegister(reg));
        } catch (const TSerialDeviceTransientErrorException& e) {
            reg->SetError();
            LOG(Warn) << "TSerialDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "]";
        } catch (const TSerialDevicePermanentRegisterException& e) {
            reg->SetAvailable(false);
            reg->SetError();
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
    return !_DeviceConfig->SetupItemConfigs.empty();
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

std::unordered_map<std::string, PProtocol> TSerialDeviceFactory::Protocols;

void TSerialDeviceFactory::RegisterProtocol(PProtocol protocol)
{
    Protocols.insert(std::make_pair(protocol->GetName(), protocol));
}

const PProtocol TSerialDeviceFactory::GetProtocolEntry(PDeviceConfig device_config)
{
    return TSerialDeviceFactory::GetProtocol(device_config->Protocol);
}

PProtocol TSerialDeviceFactory::GetProtocol(const std::string& name)
{
    auto it = Protocols.find(name);
    if (it == Protocols.end())
        throw TSerialDeviceException("unknown serial protocol: " + name);
    return it->second;
}

PSerialDevice TSerialDeviceFactory::CreateDevice(PDeviceConfig device_config, PPort port)
{
    return GetProtocolEntry(device_config)->CreateDevice(device_config, port);
}

PRegisterTypeMap TSerialDeviceFactory::GetRegisterTypes(PDeviceConfig device_config)
{
    return GetProtocolEntry(device_config)->GetRegTypes();
}

TUInt32SlaveId::TUInt32SlaveId(const std::string& slaveId)
{
    try {
        SlaveId = std::stoul(slaveId, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + slaveId + "\" is not convertible to string");
    }
}

TAggregatedSlaveId::TAggregatedSlaveId(const std::string& slaveId)
{
    try {
        auto delimiter_it = slaveId.find(':');
        PrimaryId = std::stoi(slaveId.substr(0, delimiter_it), 0, 0);
        SecondaryId = std::stoi(slaveId.substr(delimiter_it + 1), 0, 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + slaveId + "\" is not convertible to string");
    }
}

bool TAggregatedSlaveId::operator==(const TAggregatedSlaveId & other) const
{
    return PrimaryId == other.PrimaryId && SecondaryId == other.SecondaryId;
}
