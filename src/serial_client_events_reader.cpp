#include "serial_client_events_reader.h"
#include "log.h"
#include "serial_device.h"

#include <string.h>

using namespace std::chrono;

#define LOG(logger) logger.Log() << "[serial client] "

namespace
{
    const auto EVENTS_READ_INTERVAL = 50ms;

    std::string EventTypeToString(uint8_t eventType)
    {
        switch (eventType) {
            case ModbusExt::TEventRegisterType::COIL:
                return "coil";
            case ModbusExt::TEventRegisterType::DISCRETE:
                return "discrete";
            case ModbusExt::TEventRegisterType::HOLDING:
                return "holding";
            case ModbusExt::TEventRegisterType::INPUT:
                return "input";
        }
        return "unknown";
    }

    std::string MakeRegisterDescriptionString(uint8_t slaveId, uint8_t eventType, uint16_t eventId)
    {
        return "<modbus:" + std::to_string(slaveId) + ":" + EventTypeToString(eventType) + ": " +
               std::to_string(eventId) + ">";
    }

    ModbusExt::TEventRegisterType ToEventRegisterType(const Modbus::RegisterType regType)
    {
        switch (regType) {
            case Modbus::REG_COIL:
                return ModbusExt::TEventRegisterType::COIL;
            case Modbus::REG_DISCRETE:
                return ModbusExt::TEventRegisterType::DISCRETE;
            case Modbus::REG_HOLDING:
            case Modbus::REG_HOLDING_SINGLE:
            case Modbus::REG_HOLDING_MULTI:
                return ModbusExt::TEventRegisterType::HOLDING;
            case Modbus::REG_INPUT:
                return ModbusExt::TEventRegisterType::INPUT;
            default:
                throw std::runtime_error("unsupported register type");
        }
    }
};

TSerialClientEventsReader::TSerialClientEventsReader()
{}

class TModbusExtEventsVisitor: public ModbusExt::IEventsVisitor
{
    const std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher>& Regs;
    TSerialClientEventsReader::TAfterEventFn AfterEventFn;

public:
    TModbusExtEventsVisitor(const std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher>& regs,
                            TSerialClientEventsReader::TAfterEventFn afterEventFn)
        : Regs(regs),
          AfterEventFn(afterEventFn)
    {}

    virtual void Event(uint8_t slaveId,
                       uint8_t eventType,
                       uint16_t eventId,
                       const uint8_t* data,
                       size_t dataSize) override
    {
        auto reg = Regs.find({slaveId, eventId, eventType});
        if (reg != Regs.end()) {
            LOG(Info) << "Event on " << reg->second->ToString() << ", data: " << WBMQTT::HexDump(data, dataSize);
            uint64_t value = 0;
            memcpy(&value, data, std::min(dataSize, sizeof(value)));
            reg->second->SetValue(TRegisterValue(value));
            AfterEventFn(reg->second);
        } else {
            // TODO: disable events
            LOG(Warn) << "Unexpected event from: " << MakeRegisterDescriptionString(slaveId, eventType, eventId);
        }
    }
};

void TSerialClientEventsReader::ReadEvents(TPort& port,
                                           steady_clock::time_point now,
                                           milliseconds maxReadingTime,
                                           TAfterEventFn afterEventFn)
{
    if (DevicesWithEnabledEvents.empty() || now < NextEventsReadTime) {
        return;
    }
    TModbusExtEventsVisitor visitor(Regs, afterEventFn);
    NextEventsReadTime = now + EVENTS_READ_INTERVAL;
    ModbusExt::ReadEvents(port, milliseconds(100), milliseconds(100), visitor, EventState);
}

std::chrono::steady_clock::time_point TSerialClientEventsReader::GetNextEventsReadTime() const
{
    return NextEventsReadTime;
}

void TSerialClientEventsReader::EnableEvents(PSerialDevice device, TPort& port)
{
    TModbusDevice* modbusDevice = dynamic_cast<TModbusDevice*>(device.get());
    if (!modbusDevice) {
        return;
    }
    uint8_t slaveId = static_cast<uint8_t>(modbusDevice->SlaveId);
    DevicesWithEnabledEvents.erase(slaveId);
    std::chrono::milliseconds responseTimeout = std::chrono::milliseconds(100);
    std::chrono::milliseconds frameTimeout = std::chrono::milliseconds(100);
    ModbusExt::TEventsEnabler ev(slaveId,
                                 port,
                                 responseTimeout,
                                 frameTimeout,
                                 std::bind(&TSerialClientEventsReader::OnEnabledEvent,
                                           this,
                                           slaveId,
                                           std::placeholders::_1,
                                           std::placeholders::_2,
                                           std::placeholders::_3));

    try {
        std::vector<std::pair<TEventsReaderRegisterDesc, ModbusExt::TEventPriority>> regsToEnable;
        for (const auto& reg: Regs) {
            if (reg.first.SlaveId == slaveId) {
                regsToEnable.emplace_back(reg.first,
                                          reg.second->IsHighPriority() ? ModbusExt::TEventPriority::HIGH
                                                                       : ModbusExt::TEventPriority::LOW);
            }
        }
        std::sort(regsToEnable.begin(), regsToEnable.end(), [](const auto& r1, const auto& r2) {
            if (r1.first.Type == r2.first.Type) {
                return r1.first.Type < r2.first.Type;
            }
            return r1.first.Addr < r2.first.Addr;
        });
        for (const auto& reg: regsToEnable) {
            ev.AddRegister(reg.first.Addr, reg.first.Type, reg.second);
        }
        LOG(Debug) << "Try to enable events on modbus device: " << slaveId;
        ev.SendRequest();
    } catch (const std::exception& e) {
        LOG(Warn) << "Failed to enable events on modbus device: " << slaveId << ": " << e.what();
    }
}

void TSerialClientEventsReader::OnEnabledEvent(uint8_t slaveId,
                                               ModbusExt::TEventRegisterType type,
                                               uint16_t addr,
                                               bool res)
{
    auto reg = Regs.find({slaveId, addr, type});

    if (reg != Regs.end()) {
        if (res) {
            reg->second->ExcludeFromPolling();
            DevicesWithEnabledEvents.emplace(slaveId);
        } else {
            reg->second->IncludeInPolling();
        }
    }
    LOG(Info) << "Events are " << (res ? "enabled for" : "disabled for")
              << MakeRegisterDescriptionString(slaveId, type, addr);
}

void TSerialClientEventsReader::AddRegister(PRegister reg)
{
    auto dev = dynamic_cast<TModbusDevice*>(reg->Device().get());
    if (reg->SporadicMode != TRegisterConfig::TSporadicMode::DISABLED && dev != nullptr) {
        Regs.emplace(TEventsReaderRegisterDesc{static_cast<uint8_t>(dev->SlaveId),
                                               static_cast<uint16_t>(GetUint32RegisterAddress(reg->GetAddress())),
                                               ToEventRegisterType(static_cast<Modbus::RegisterType>(reg->Type))},
                     reg);
    }
}
