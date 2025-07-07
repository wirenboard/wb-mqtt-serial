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

TDeviceSetupItem::TDeviceSetupItem(PDeviceSetupItemConfig config, PSerialDevice device)
    : Name(config->GetName()),
      ParameterId(config->GetParameterId()),
      RawValue(config->GetRawValue()),
      HumanReadableValue(config->GetValue()),
      RegisterConfig(config->GetRegisterConfig()),
      Device(device)
{}

std::string TDeviceSetupItem::ToString()
{
    std::stringstream stream;
    auto reg = "<" + (Device ? Device->ToString() : "unknown device") + ":" + RegisterConfig->ToString() + ">";
    stream << "Init setup register \"" << Name << "\": " << reg << "<-- " << HumanReadableValue;
    if (RawValue.GetType() == TRegisterValue::ValueType::String) {
        stream << " (\"" << RawValue << "\")";
    } else {
        stream << " (0x" << std::hex << RawValue << ")";
    }
    return stream.str();
}

bool TDeviceSetupItemComparePredicate::operator()(const PDeviceSetupItem& a, const PDeviceSetupItem& b) const
{
    if (a->RegisterConfig->Type != b->RegisterConfig->Type) {
        return a->RegisterConfig->Type < b->RegisterConfig->Type;
    }
    auto compare = a->RegisterConfig->GetWriteAddress().Compare(b->RegisterConfig->GetWriteAddress());
    if (compare == 0) {
        return a->RegisterConfig->GetDataOffset() < b->RegisterConfig->GetDataOffset();
    }
    return compare < 0;
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
      SupportsHoles(true),
      SporadicOnly(true)
{}

std::string TSerialDevice::ToString() const
{
    auto portDescription = Port()->GetDescription();
    if (!portDescription.empty()) {
        portDescription += " ";
    }
    return portDescription + Protocol()->GetName() + ":" + DeviceConfig()->SlaveId;
}

PRegisterRange TSerialDevice::CreateRegisterRange() const
{
    return PRegisterRange(new TSameAddressRegisterRange());
}

void TSerialDevice::Prepare(TDevicePrepareMode prepareMode)
{
    bool deviceWasDisconnected = (ConnectionState != TDeviceConnectionState::CONNECTED);
    try {
        PrepareImpl();
        if (prepareMode == TDevicePrepareMode::WITH_SETUP_IF_WAS_DISCONNECTED && deviceWasDisconnected) {
            WriteSetupRegisters(GetSetupItems());
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
        WriteRegisterImpl(*reg->GetConfig(), value);
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

TRegisterValue TSerialDevice::ReadRegisterImpl(const TRegisterConfig& reg)
{
    throw TSerialDeviceException("single register reading is not supported");
}

void TSerialDevice::WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value)
{
    throw TSerialDeviceException(ToString() + ": register writing is not supported");
}

void TSerialDevice::ReadRegisterRange(PRegisterRange range)
{
    for (auto& reg: range->RegisterList()) {
        try {
            if (reg->GetAvailable() != TRegisterAvailability::UNAVAILABLE) {
                Port()->SleepSinceLastInteraction(DeviceConfig()->RequestDelay);
                auto value = ReadRegisterImpl(*reg->GetConfig());
                SetTransferResult(true);
                reg->SetValue(value);
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

void TSerialDevice::WriteSetupRegisters(const TDeviceSetupItems& setupItems)
{
    for (const auto& item: setupItems) {
        WriteRegisterImpl(*item->RegisterConfig, item->RawValue);
        LOG(Info) << item->ToString();
    }
    if (!setupItems.empty()) {
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

bool TSerialDevice::IsSporadicOnly() const
{
    return SporadicOnly;
}

void TSerialDevice::SetSporadicOnly(bool sporadicOnly)
{
    SporadicOnly = sporadicOnly;
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
        // clear serial number on connection as it can be other device than before disconnection
        if (SnRegister) {
            SnRegister->SetValue(TRegisterValue());
        }
        LOG(Info) << "device " << ToString() << " is connected";
    } else {
        LOG(Warn) << "device " << ToString() << " is disconnected";
    }
    for (auto& callback: ConnectionStateChangedCallbacks) {
        callback(shared_from_this());
    }
}

PRegister TSerialDevice::GetSnRegister() const
{
    return SnRegister;
}

void TSerialDevice::SetSnRegister(PRegisterConfig regConfig)
{
    auto regIt = std::find_if(Registers.begin(), Registers.end(), [&regConfig](const PRegister& reg) {
        return reg->GetConfig() == regConfig;
    });
    if (regIt == Registers.end()) {
        SnRegister = std::make_shared<TRegister>(shared_from_this(), regConfig);
    } else {
        SnRegister = *regIt;
    }
}

void TSerialDevice::AddSetupItem(PDeviceSetupItemConfig item)
{
    auto key = item->GetRegisterConfig()->ToString();
    auto addrIt = SetupItemsByAddress.find(key);
    if (addrIt != SetupItemsByAddress.end()) {
        std::stringstream ss;
        ss << "Setup command \"" << item->GetName() << "\" (" << key << ") from \"" << DeviceConfig()->DeviceType
           << "\" has a duplicate command \"" << addrIt->second->Name << "\" ";
        if (item->GetRawValue() == addrIt->second->RawValue) {
            ss << "with the same register value.";
        } else {
            ss << "with different register value. IT WILL BREAK TEMPLATE OPERATION";
        }
        LOG(Warn) << ss.str();
    } else {
        auto setupItem = std::make_shared<TDeviceSetupItem>(item, shared_from_this());
        SetupItemsByAddress.insert({key, setupItem});
        SetupItems.insert(setupItem);
    }
}

const TDeviceSetupItems& TSerialDevice::GetSetupItems() const
{
    return SetupItems;
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

TDeviceConfig::TDeviceConfig(const std::string& name, const std::string& slave_id, const std::string& protocol)
    : Name(name),
      SlaveId(slave_id),
      Protocol(protocol)
{}
