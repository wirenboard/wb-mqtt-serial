#pragma once

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

#include "modbus_device.h"
#include "running_average.h"

class TModbusIODevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;
    int Shift = 0;
    Modbus::TRegisterCache ModbusCache;
    TRunningAverage<std::chrono::microseconds, 10> ResponseTime;

public:
    TModbusIODevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                    const TModbusDeviceConfig& config,
                    PProtocol protocol);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(TPort& port, PRegisterRange range) override;
    static void Register(TSerialDeviceFactory& factory);

    std::chrono::milliseconds GetFrameTimeout(TPort& port) const override;

protected:
    void PrepareImpl(TPort& port) override;
    void WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& value) override;
    void WriteSetupRegisters(TPort& port) override;
};
