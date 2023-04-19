#pragma once

#include <chrono>

#include <unordered_map>

#include "devices/modbus_device.h"
#include "modbus_ext_common.h"
#include "port.h"
#include "register.h"

struct TEventsReaderRegisterDesc
{
    uint8_t SlaveId;
    uint16_t Addr;
    uint8_t Type;

    bool operator==(const TEventsReaderRegisterDesc& other) const
    {
        return SlaveId == other.SlaveId && Addr == other.Addr && Type == other.Type;
    }
};

struct TRegisterDescHasher: public std::unary_function<TEventsReaderRegisterDesc, size_t>
{
public:
    size_t operator()(const TEventsReaderRegisterDesc& reg) const
    {
        return (reg.SlaveId << 24) + (reg.Addr << 8) + reg.Type;
    }
};

class TSerialClientEventsReader
{
public:
    typedef std::function<void(PRegister)> TAfterEventFn;

    TSerialClientEventsReader();

    void AddRegister(PRegister reg);

    void EnableEvents(PSerialDevice device, TPort& port);

    void ReadEvents(TPort& port,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds maxReadingTime,
                    TAfterEventFn afterEventFn);

    std::chrono::steady_clock::time_point GetNextEventsReadTime() const;

private:
    ModbusExt::TEventConfirmationState EventState;
    std::chrono::steady_clock::time_point NextEventsReadTime;

    std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher> Regs;
    std::unordered_set<uint8_t> DevicesWithEnabledEvents;

    void OnEnabledEvent(uint8_t slaveId, ModbusExt::TEventRegisterType type, uint16_t addr, bool res);
};
