#pragma once

#include <chrono>

#include <unordered_map>

#include "common_utils.h"
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

template<> struct std::hash<TEventsReaderRegisterDesc>
{
    size_t operator()(const TEventsReaderRegisterDesc& reg) const noexcept
    {
        return (reg.SlaveId << 24) + (reg.Addr << 8) + reg.Type;
    }
};

class TSerialClientEventsReader
{
public:
    typedef std::function<void(PRegister)> TRegisterCallback;
    typedef std::function<void(PSerialDevice)> TDeviceCallback;

    // Several TRegister objects can have same modbus address,
    // but represent different value regions
    typedef std::unordered_map<TEventsReaderRegisterDesc, std::vector<PRegister>> TRegsMap;

    TSerialClientEventsReader(size_t maxReadErrors);

    void AddRegister(PRegister reg);

    void EnableEvents(PSerialDevice device, TPort& port);

    void ReadEvents(TPort& port,
                    std::chrono::milliseconds maxReadingTime,
                    TRegisterCallback registerCallback,
                    TDeviceCallback deviceRestartedHandler,
                    util::TGetNowFn nowFn);

    void DeviceDisconnected(PSerialDevice device);
    void SetReadErrors(TRegisterCallback callback);

    bool HasDevicesWithEnabledEvents() const;

private:
    uint8_t LastAccessedSlaveId;
    ModbusExt::TEventConfirmationState EventState;
    size_t ReadErrors;
    size_t MaxReadErrors;
    bool ClearErrorsOnSuccessfulRead;

    TRegsMap Regs;
    std::unordered_set<uint8_t> DevicesWithEnabledEvents;

    void OnEnabledEvent(uint8_t slaveId, uint8_t type, uint16_t addr, bool res);
    void ClearReadErrors(TRegisterCallback callback);
    void ReadEventsFailed(const std::string& errorMessage, TRegisterCallback registerCallback);
};
