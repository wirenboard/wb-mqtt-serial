#include "serial_device.h"

#include "log.h"
#include <iostream>
#include <string.h>
#include <unistd.h>

#define LOG(logger) logger.Log() << "[serial device] "

IProtocol::IProtocol(const std::string& name, const TRegisterTypes& reg_types)
    : Name(name),
      RegTypes(std::make_shared<TRegisterTypeMap>(reg_types))
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

TUint32SlaveIdProtocol::TUint32SlaveIdProtocol(const std::string& name,
                                               const TRegisterTypes& reg_types,
                                               bool allowBroadcast)
    : IProtocol(name, reg_types),
      AllowBroadcast(allowBroadcast)
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
    : SerialPort(port),
      _DeviceConfig(config),
      _Protocol(protocol),
      LastSuccessfulCycle(),
      ConnectionState(TDeviceConnectionState::UNKNOWN),
      RemainingFailCycles(0),
      SupportsHoles(true)
{}

std::string TSerialDevice::ToString() const
{
    return Protocol()->GetName() + ":" + DeviceConfig()->SlaveId;
}

PRegisterRange TSerialDevice::CreateRegisterRange() const
{
    return PRegisterRange(new TSameAddressRegisterRange());
}

void TSerialDevice::Prepare()
{
    bool deviceWasDisconnected = (ConnectionState != TDeviceConnectionState::CONNECTED);
    try {
        PrepareImpl();
        if (deviceWasDisconnected) {
            WriteSetupRegisters();
        }
    } catch (const TSerialDeviceException& ex) {
        SetTransferResult(false);
        throw;
    }
}

void TSerialDevice::PrepareImpl()
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
}

void TSerialDevice::EndSession()
{}

void TSerialDevice::InvalidateReadCache()
{}

void TSerialDevice::WriteRegister(PRegister reg, const TRegisterValue& value)
{
    try {
        WriteRegisterImpl(reg, value);
        SetTransferResult(true);
    } catch (const TSerialDevicePermanentRegisterException& e) {
        SetTransferResult(true);
        throw;
    } catch (const TSerialDeviceException& ex) {
        SetTransferResult(false);
        throw;
    }
}

void TSerialDevice::WriteRegister(PRegister reg, uint64_t value)
{
    WriteRegister(reg, TRegisterValue{value});
}

TRegisterValue TSerialDevice::ReadRegisterImpl(PRegister reg)
{
    throw TSerialDeviceException("single register reading is not supported");
}

void TSerialDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& value)
{
    throw TSerialDeviceException(ToString() + ": register writing is not supported");
}

void TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    for (auto& reg: range->RegisterList()) {
        try {
            if (reg->GetAvailable() != TRegisterAvailability::UNAVAILABLE) {
                Port()->SleepSinceLastInteraction(DeviceConfig()->RequestDelay);
                reg->SetValue(ReadRegisterImpl(reg));
                SetTransferResult(true);
            }
        } catch (const TSerialDeviceInternalErrorException& e) {
            reg->SetError(TRegister::TError::ReadError);
            LOG(Warn) << "TSerialDevice::ReadRegister(): " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "]";
            SetTransferResult(true);
        } catch (const TSerialDevicePermanentRegisterException& e) {
            reg->SetAvailable(TRegisterAvailability::UNAVAILABLE);
            reg->SetError(TRegister::TError::ReadError);
            LOG(Warn) << "TSerialDevice::ReadRegister(): " << e.what() << " [slave_id is "
                      << reg->Device()->ToString() + "] Register " << reg->ToString()
                      << " is now marked as unsupported";
            SetTransferResult(true);
        } catch (const TSerialDeviceException& e) {
            reg->SetError(TRegister::TError::ReadError);
            auto& logger = (ConnectionState == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
            LOG(logger) << "TSerialDevice::ReadRegister(): " << e.what() << " [slave_id is "
                        << reg->Device()->ToString() + "]";
            SetTransferResult(false);
        }
    }
    InvalidateReadCache();
}

void TSerialDevice::SetTransferResult(bool ok)
{
    // disable reconnect functionality option
    if (_DeviceConfig->DeviceTimeout.count() < 0 || _DeviceConfig->DeviceMaxFailCycles < 0) {
        return;
    }

    if (ok) {
        LastSuccessfulCycle = std::chrono::steady_clock::now();
        SetConnectionState(TDeviceConnectionState::CONNECTED);
        RemainingFailCycles = _DeviceConfig->DeviceMaxFailCycles;
    } else {

        if (RemainingFailCycles > 0) {
            --RemainingFailCycles;
        }

        if (RemainingFailCycles == 0 && (ConnectionState != TDeviceConnectionState::DISCONNECTED) &&
            (std::chrono::steady_clock::now() - LastSuccessfulCycle > _DeviceConfig->DeviceTimeout))
        {
            SetDisconnected();
        }
    }
}

TDeviceConnectionState TSerialDevice::GetConnectionState() const
{
    return ConnectionState;
}

void TSerialDevice::InitSetupItems()
{
    for (auto& setup_item_config: _DeviceConfig->SetupItemConfigs) {
        SetupItems.push_back(std::make_shared<TDeviceSetupItem>(
            setup_item_config,
            std::make_shared<TRegister>(shared_from_this(), setup_item_config->GetRegisterConfig())));
    }
}

void TSerialDevice::WriteSetupRegisters()
{
    for (const auto& setup_item: SetupItems) {
        WriteRegisterImpl(setup_item->Register, setup_item->RawValue);

        std::stringstream ss;
        ss << "Init: " << setup_item->Name << ": setup register " << setup_item->Register->ToString() << " <-- "
           << setup_item->HumanReadableValue;

        if (setup_item->RawValue.GetType() == TRegisterValue::ValueType::String) {
            ss << " ('" << setup_item->RawValue << "')";
        } else {
            ss << " (0x" << std::hex << setup_item->RawValue << ")";
            // TODO: More verbose exception
        }
        LOG(Info) << ss.str();
    }
    if (!SetupItems.empty()) {
        SetTransferResult(true);
    }
}

PPort TSerialDevice::Port() const
{
    return SerialPort;
}

PDeviceConfig TSerialDevice::DeviceConfig() const
{
    return _DeviceConfig;
}

PProtocol TSerialDevice::Protocol() const
{
    return _Protocol;
}

bool TSerialDevice::GetSupportsHoles() const
{
    return SupportsHoles;
}

void TSerialDevice::SetSupportsHoles(bool supportsHoles)
{
    SupportsHoles = supportsHoles;
}

void TSerialDevice::SetDisconnected()
{
    SetSupportsHoles(true);
    SetConnectionState(TDeviceConnectionState::DISCONNECTED);
}

PRegister TSerialDevice::AddRegister(PRegisterConfig config)
{
    auto reg = std::make_shared<TRegister>(shared_from_this(), config);
    Registers.push_back(reg);
    return reg;
}

const std::list<PRegister>& TSerialDevice::GetRegisters() const
{
    return Registers;
}

std::chrono::steady_clock::time_point TSerialDevice::GetLastReadTime() const
{
    return LastReadTime;
}

void TSerialDevice::SetLastReadTime(std::chrono::steady_clock::time_point readTime)
{
    LastReadTime = readTime;
}

void TSerialDevice::AddOnConnectionStateChangedCallback(TSerialDevice::TDeviceCallback callback)
{
    ConnectionStateChangedCallbacks.push_back(callback);
}

void TSerialDevice::SetConnectionState(TDeviceConnectionState state)
{
    if (ConnectionState == state) {
        return;
    }
    ConnectionState = state;
    if (state == TDeviceConnectionState::CONNECTED) {
        LOG(Info) << "device " << ToString() << " is connected";
    } else {
        LOG(Warn) << "device " << ToString() << " is disconnected";
    }
    for (auto& callback: ConnectionStateChangedCallbacks) {
        callback(shared_from_this());
    }
}

TUInt32SlaveId::TUInt32SlaveId(const std::string& slaveId, bool allowBroadcast): HasBroadcastSlaveId(false)
{
    if (allowBroadcast) {
        if (slaveId.empty()) {
            SlaveId = 0;
            HasBroadcastSlaveId = true;
            return;
        }
    }
    try {
        SlaveId = std::stoul(slaveId, /* pos = */ 0, /* base = */ 0);
    } catch (const std::logic_error& e) {
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

uint64_t CopyDoubleToUint64(double value)
{
    uint64_t res = 0;
    static_assert((sizeof(res) >= sizeof(value)), "Can't fit double into uint64_t");
    memcpy(&res, &value, sizeof(value));
    return res;
}
