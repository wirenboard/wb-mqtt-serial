#include "serial_device.h"

#include <iostream>
#include <unistd.h>
#include "log.h"

#define LOG(logger) logger.Log() << "[serial device] "

IProtocol::IProtocol(const std::string& name, const TRegisterTypes& reg_types)
    : Name(name), RegTypes(std::make_shared<TRegisterTypeMap>(reg_types))
{}

const std::string& IProtocol::GetName() const
{ 
    return Name;
}

PRegisterTypeMap IProtocol::GetRegTypes() const
{
    return RegTypes;
}

bool IProtocol::IsModbus() const
{
    return false;
}

bool IProtocol::SupportsBroadcast() const
{
    return false;
}

TUint32SlaveIdProtocol::TUint32SlaveIdProtocol(const std::string& name, const TRegisterTypes& reg_types, bool allowBroadcast) 
    : IProtocol(name, reg_types), AllowBroadcast(allowBroadcast)
{}

bool TUint32SlaveIdProtocol::IsSameSlaveId(const std::string& id1, const std::string& id2) const
{
    return (TUInt32SlaveId(id1, AllowBroadcast) == TUInt32SlaveId(id2, AllowBroadcast));
}

bool TUint32SlaveIdProtocol::SupportsBroadcast() const
{
    return AllowBroadcast;
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
        } catch (const TSerialDeviceInternalErrorException& e) {
            reg->SetError(ST_DEVICE_ERROR);
            LOG(Warn) << "TSerialDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "]";
        } catch (const TSerialDeviceTransientErrorException& e) {
            reg->SetError(ST_UNKNOWN_ERROR);
            auto& logger = GetIsDisconnected() ? Debug : Warn;
            LOG(logger) << "TSerialDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                        << reg->Device()->ToString() + "]";
        } catch (const TSerialDevicePermanentRegisterException& e) {
            reg->SetAvailable(false);
            reg->SetError(ST_DEVICE_ERROR);
            LOG(Warn) << "TSerialDevice::ReadRegisterRange(): " << e.what() << " [slave_id is "
                  << reg->Device()->ToString() + "] Register " << reg->ToString() << " is now marked as unsupported";
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

        if (RemainingFailCycles > 0) {
            --RemainingFailCycles;
        }

        if ((std::chrono::steady_clock::now() - LastSuccessfulCycle > _DeviceConfig->DeviceTimeout) &&
            RemainingFailCycles == 0 &&
            (!IsDisconnected || LastSuccessfulCycle == std::chrono::steady_clock::time_point()))
        {
            IsDisconnected = true;
            LastSuccessfulCycle = std::chrono::steady_clock::now();
            LOG(Info) << "device " << ToString() << " is disconnected";
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
            WriteRegister(setup_item->Register, setup_item->RawValue);
            LOG(Info) << "Init: " << setup_item->Name 
                      << ": setup register " << setup_item->Register->ToString()
                      << " <-- " << setup_item->HumanReadableValue << " (0x" << std::hex << setup_item->RawValue << ")";
        } catch (const TSerialDeviceException & e) {
            LOG(Warn) << "failed to write: " << setup_item->Register->ToString() << ": " << e.what();
            return false;
        }
    }
    return true;
}

TUInt32SlaveId::TUInt32SlaveId(const std::string& slaveId, bool allowBroadcast): HasBroadcastSlaveId(false)
{
    if (allowBroadcast) {
        if (slaveId.empty()) {
            HasBroadcastSlaveId = true;
            return;
        }
    }
    try {
        SlaveId = std::stoul(slaveId, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error &e) {
        throw TSerialDeviceException("slave ID \"" + slaveId + "\" is not convertible to integer");
    }
}

bool TUInt32SlaveId::operator==(const TUInt32SlaveId& id) const
{
    if (HasBroadcastSlaveId || id.HasBroadcastSlaveId) {
        return true;
    }
    return SlaveId == id.SlaveId;
}
