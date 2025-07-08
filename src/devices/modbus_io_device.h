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
                    PPort port,
                    PProtocol protocol);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(PRegisterRange range) override;
    void WriteSetupRegisters(const TDeviceSetupItems& setupItems, bool breakOnError = false) override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    void PrepareImpl() override;
    void WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value) override;
};
