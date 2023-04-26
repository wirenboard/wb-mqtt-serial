#include "serial_client_events_reader.h"
#include "common_utils.h"
#include "log.h"
#include "serial_device.h"

#include <string.h>

using namespace std::chrono;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    std::string EventTypeToString(uint8_t eventType)
    {
        switch (eventType) {
            case ModbusExt::TEventType::COIL:
                return "coil";
            case ModbusExt::TEventType::DISCRETE:
                return "discrete";
            case ModbusExt::TEventType::HOLDING:
                return "holding";
            case ModbusExt::TEventType::INPUT:
                return "input";
        }
        return "unknown";
    }

    std::string MakeDeviceDescriptionString(uint8_t slaveId)
    {
        return "modbus:" + std::to_string(slaveId);
    }

    std::string MakeRegisterDescriptionString(uint8_t slaveId, uint8_t eventType, uint16_t eventId)
    {
        return "<" + MakeDeviceDescriptionString(slaveId) + ":" + EventTypeToString(eventType) + ": " +
               std::to_string(eventId) + ">";
    }

    ModbusExt::TEventType ToEventRegisterType(const Modbus::RegisterType regType)
    {
        switch (regType) {
            case Modbus::REG_COIL:
                return ModbusExt::TEventType::COIL;
            case Modbus::REG_DISCRETE:
                return ModbusExt::TEventType::DISCRETE;
            case Modbus::REG_HOLDING:
            case Modbus::REG_HOLDING_SINGLE:
            case Modbus::REG_HOLDING_MULTI:
                return ModbusExt::TEventType::HOLDING;
            case Modbus::REG_INPUT:
                return ModbusExt::TEventType::INPUT;
            default:
                throw std::runtime_error("unsupported register type");
        }
    }

    TModbusDevice* ToModbusDevice(TSerialDevice* device)
    {
        // Only MODBUS RTU devices
        auto dev = dynamic_cast<TModbusDevice*>(device);
        if (dev != nullptr && dev->Protocol()->GetName() == "modbus") {
            return dev;
        }
        return nullptr;
    }

    void DisableEventsFromRegs(TPort& port, const std::list<TEventsReaderRegisterDesc>& regs)
    {
        if (regs.empty()) {
            return;
        }
        for (auto regIt = regs.cbegin(); regIt != regs.cend(); ++regIt) {
            uint8_t slaveId = regIt->SlaveId;
            ModbusExt::TEventsEnabler enabler(slaveId, port, 100ms, 100ms, [](uint8_t, uint16_t, bool) {});
            for (; regIt != regs.cend() && slaveId == regIt->SlaveId; ++regIt) {
                enabler.AddRegister(regIt->Addr,
                                    static_cast<ModbusExt::TEventType>(regIt->Type),
                                    ModbusExt::TEventPriority::DISABLE);
            }
            try {
                enabler.SendRequests();
            } catch (const TSerialDeviceException& ex) {
                LOG(Warn) << "Failed to disable unexpected events: " << ex.what();
            }
        }
    }
};

class TModbusExtEventsVisitor: public ModbusExt::IEventsVisitor
{
    const std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher>& Regs;
    std::unordered_set<uint8_t>& DevicesWithEnabledEvents;
    TSerialClientEventsReader::TRegisterCallback RegisterChangedCallback;
    TSerialClientEventsReader::TDeviceCallback DeviceRestartedCallback;
    uint8_t SlaveId = 0;
    std::list<TEventsReaderRegisterDesc> RegsToDisable;

    void ProcessRegisterChangeEvent(uint8_t slaveId,
                                    uint8_t eventType,
                                    uint16_t eventId,
                                    const uint8_t* data,
                                    size_t dataSize)
    {
        auto reg = Regs.find({slaveId, eventId, eventType});
        if (reg != Regs.end()) {
            LOG(Debug) << "Event from " << reg->second->ToString() << ", data: " << WBMQTT::HexDump(data, dataSize);
            uint64_t value = 0;
            memcpy(&value, data, std::min(dataSize, sizeof(value)));
            reg->second->SetValue(TRegisterValue(value));
            RegisterChangedCallback(reg->second);
        } else {
            LOG(Warn) << "Unexpected event from: " << MakeRegisterDescriptionString(slaveId, eventType, eventId);
            RegsToDisable.emplace_back(TEventsReaderRegisterDesc{slaveId, eventId, eventType});
        }
    }

    void ProcessDeviceRestartedEvent(uint8_t slaveId)
    {
        DevicesWithEnabledEvents.erase(slaveId);
        for (const auto& reg: Regs) {
            if (reg.first.SlaveId == slaveId) {
                LOG(Debug) << "Restart event from " << MakeDeviceDescriptionString(slaveId);
                DeviceRestartedCallback(reg.second->Device());
                return;
            }
        }
        LOG(Warn) << "Restart event from unknown device " << MakeDeviceDescriptionString(slaveId);
    }

public:
    TModbusExtEventsVisitor(const std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher>& regs,
                            std::unordered_set<uint8_t>& devicesWithEnabledEvents,
                            TSerialClientEventsReader::TRegisterCallback registerChangedCallback,
                            TSerialClientEventsReader::TDeviceCallback deviceRestartedCallback)
        : Regs(regs),
          DevicesWithEnabledEvents(devicesWithEnabledEvents),
          RegisterChangedCallback(registerChangedCallback),
          DeviceRestartedCallback(deviceRestartedCallback)
    {}

    virtual void Event(uint8_t slaveId,
                       uint8_t eventType,
                       uint16_t eventId,
                       const uint8_t* data,
                       size_t dataSize) override
    {
        SlaveId = slaveId;
        switch (eventType) {
            case ModbusExt::TEventType::COIL:
            case ModbusExt::TEventType::DISCRETE:
            case ModbusExt::TEventType::HOLDING:
            case ModbusExt::TEventType::INPUT: {
                ProcessRegisterChangeEvent(slaveId, eventType, eventId, data, dataSize);
                return;
            }
            case ModbusExt::REBOOT: {
                ProcessDeviceRestartedEvent(slaveId);
                return;
            }
            default:
                LOG(Warn) << "Unexpected event from " << MakeDeviceDescriptionString(slaveId)
                          << " type: " << static_cast<int>(eventType) << ", id: " << eventId;
                break;
        }
    }

    uint8_t GetSlaveId() const
    {
        return SlaveId;
    }

    const std::list<TEventsReaderRegisterDesc>& GetRegsToDisable() const
    {
        return RegsToDisable;
    }
};

TSerialClientEventsReader::TSerialClientEventsReader(size_t maxReadErrors)
    : LastAccessedSlaveId(0),
      ReadErrors(0),
      MaxReadErrors(maxReadErrors)
{}

bool TSerialClientEventsReader::ReadEvents(TPort& port,
                                           milliseconds maxReadingTime,
                                           TRegisterCallback registerCallback,
                                           TDeviceCallback deviceRestartedHandler)
{
    if (DevicesWithEnabledEvents.empty()) {
        return false;
    }
    TModbusExtEventsVisitor visitor(Regs, DevicesWithEnabledEvents, registerCallback, deviceRestartedHandler);
    util::TSpendTimeMeter spendTimeMeter;
    spendTimeMeter.Start();
    for (auto spendTime = 0us; spendTime < maxReadingTime; spendTime = spendTimeMeter.GetSpendTime()) {
        try {
            if (!ModbusExt::ReadEvents(port,
                                       100ms,
                                       100ms,
                                       floor<milliseconds>(maxReadingTime - spendTime),
                                       LastAccessedSlaveId,
                                       EventState,
                                       visitor))
            {
                LastAccessedSlaveId = 0;
                ReadErrors = 0;
                break;
            }
            // TODO: Limit reads from same slaveId
            LastAccessedSlaveId = visitor.GetSlaveId();
            ReadErrors = 0;
        } catch (const TSerialDeviceException& ex) {
            LOG(Warn) << "Reading events failed: " << ex.what();
            ++ReadErrors;
            if (ReadErrors > MaxReadErrors) {
                SetReadErrors(registerCallback);
                ReadErrors = 0;
            }
        }
    }
    DisableEventsFromRegs(port, visitor.GetRegsToDisable());
    return true;
}

void TSerialClientEventsReader::EnableEvents(PSerialDevice device, TPort& port)
{
    auto modbusDevice = ToModbusDevice(device.get());
    if (!modbusDevice) {
        return;
    }
    uint8_t slaveId = static_cast<uint8_t>(modbusDevice->SlaveId);
    DevicesWithEnabledEvents.erase(slaveId);
    ModbusExt::TEventsEnabler ev(slaveId,
                                 port,
                                 100ms,
                                 100ms,
                                 std::bind(&TSerialClientEventsReader::OnEnabledEvent,
                                           this,
                                           slaveId,
                                           std::placeholders::_1,
                                           std::placeholders::_2,
                                           std::placeholders::_3));

    try {
        for (const auto& reg: Regs) {
            if (reg.first.SlaveId == slaveId) {
                ev.AddRegister(reg.first.Addr,
                               static_cast<ModbusExt::TEventType>(reg.first.Type),
                               reg.second->IsHighPriority() ? ModbusExt::TEventPriority::HIGH
                                                            : ModbusExt::TEventPriority::LOW);
            }
        }
        LOG(Debug) << "Try to enable events for " << MakeDeviceDescriptionString(slaveId);
        ev.SendRequests();
    } catch (const TSerialDevicePermanentRegisterException& e) {
        LOG(Warn) << "Failed to enable events for " << MakeDeviceDescriptionString(slaveId) << ": " << e.what();
    } catch (const TSerialDeviceTransientErrorException& e) {
        throw TSerialDeviceTransientErrorException(std::string("Failed to enable events: ") + e.what());
    }
}

void TSerialClientEventsReader::OnEnabledEvent(uint8_t slaveId, uint8_t type, uint16_t addr, bool res)
{
    auto reg = Regs.find({slaveId, addr, type});
    if (reg != Regs.end()) {
        if (res) {
            reg->second->ExcludeFromPolling();
            reg->second->SetAvailable(TRegisterAvailability::AVAILABLE);
            DevicesWithEnabledEvents.emplace(slaveId);
        } else {
            reg->second->IncludeInPolling();
        }
    }
    LOG(Info) << "Events are " << (res ? "enabled for " : "disabled for ")
              << MakeRegisterDescriptionString(slaveId, type, addr);
}

void TSerialClientEventsReader::AddRegister(PRegister reg)
{
    if (reg->SporadicMode != TRegisterConfig::TSporadicMode::DISABLED) {
        auto dev = ToModbusDevice(reg->Device().get());
        if (dev != nullptr) {
            Regs.emplace(TEventsReaderRegisterDesc{static_cast<uint8_t>(dev->SlaveId),
                                                   static_cast<uint16_t>(GetUint32RegisterAddress(reg->GetAddress())),
                                                   ToEventRegisterType(static_cast<Modbus::RegisterType>(reg->Type))},
                         reg);
        }
    }
}

void TSerialClientEventsReader::DeviceDisconnected(PSerialDevice device)
{
    auto dev = ToModbusDevice(device.get());
    if (dev != nullptr) {
        DevicesWithEnabledEvents.erase(dev->SlaveId);
    }
}

void TSerialClientEventsReader::SetReadErrors(TRegisterCallback callback)
{
    for (const auto& reg: Regs) {
        if (reg.second->IsExcludedFromPolling()) {
            reg.second->SetError(TRegister::TError::ReadError);
            if (callback) {
                callback(reg.second);
            }
        }
    }
}

bool TSerialClientEventsReader::HasRegisters() const
{
    return !Regs.empty();
}

bool TEventsReaderRegisterDesc::operator==(const TEventsReaderRegisterDesc& other) const
{
    return SlaveId == other.SlaveId && Addr == other.Addr && Type == other.Type;
}

size_t TRegisterDescHasher::operator()(const TEventsReaderRegisterDesc& reg) const
{
    return (reg.SlaveId << 24) + (reg.Addr << 8) + reg.Type;
}
