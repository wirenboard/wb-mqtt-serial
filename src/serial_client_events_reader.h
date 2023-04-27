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

    // Several TRegister objects can have same modbus address,
    // but represent different value regions
    typedef std::unordered_map<TEventsReaderRegisterDesc, std::vector<PRegister>, TRegisterDescHasher> TRegsMap;

    TSerialClientEventsReader(size_t maxReadErrors);

    void AddRegister(PRegister reg);

    void EnableEvents(PSerialDevice device, TPort& port);

    bool ReadEvents(TPort& port,
                    std::chrono::milliseconds maxReadingTime,
                    TRegisterCallback registerCallback,
                    TDeviceCallback deviceRestartedHandler);

    void DeviceDisconnected(PSerialDevice device);
    void SetReadErrors(TRegisterCallback callback);

    bool HasRegisters() const;

private:
    uint8_t LastAccessedSlaveId;
    ModbusExt::TEventConfirmationState EventState;
    size_t ReadErrors;
    size_t MaxReadErrors;

    TRegsMap Regs;
    std::unordered_set<uint8_t> DevicesWithEnabledEvents;

    void OnEnabledEvent(uint8_t slaveId, uint8_t type, uint16_t addr, bool res);
};
