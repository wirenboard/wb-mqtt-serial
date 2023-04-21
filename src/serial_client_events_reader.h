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

    bool operator==(const TEventsReaderRegisterDesc& other) const;
};

struct TRegisterDescHasher: public std::unary_function<TEventsReaderRegisterDesc, size_t>
{
public:
    size_t operator()(const TEventsReaderRegisterDesc& reg) const;
};

class TSerialClientEventsReader
{
public:
    typedef std::function<void(PRegister)> TRegisterCallback;
    typedef std::function<void(PSerialDevice)> TDeviceCallback;

    TSerialClientEventsReader(std::chrono::milliseconds readPeriod, size_t maxReadErrors);

    void AddRegister(PRegister reg);

    void EnableEvents(PSerialDevice device, TPort& port);

    void ReadEvents(TPort& port,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds maxReadingTime,
                    TRegisterCallback registerCallback,
                    TDeviceCallback deviceRestartedHandler);

    std::chrono::steady_clock::time_point GetNextEventsReadTime() const;

    void DeviceDisconnected(PSerialDevice device);
    void SetReadErrors(TRegisterCallback callback);

private:
    uint8_t LastAccessedSlaveId;
    ModbusExt::TEventConfirmationState EventState;
    std::chrono::steady_clock::time_point NextEventsReadTime;
    std::chrono::milliseconds ReadPeriod;
    size_t ReadErrors;
    size_t MaxReadErrors;

    std::unordered_map<TEventsReaderRegisterDesc, PRegister, TRegisterDescHasher> Regs;
    std::unordered_set<uint8_t> DevicesWithEnabledEvents;

    void OnEnabledEvent(uint8_t slaveId, ModbusExt::TEventRegisterType type, uint16_t addr, bool res);
};
