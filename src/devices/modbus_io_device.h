#pragma once

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

#include "serial_device.h"

#include "modbus_common.h"

class TModbusIODevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;
    int Shift = 0;
    Modbus::TRegisterCache ModbusCache;

public:
    TModbusIODevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                    PDeviceConfig config,
                    PPort port,
                    PProtocol protocol);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(PRegisterRange range) override;
    void WriteSetupRegisters() override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    void WriteRegisterImpl(PRegister reg, const TChannelValue& value) override;
};
