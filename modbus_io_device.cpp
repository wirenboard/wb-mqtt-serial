#include <iostream>
#include <stdexcept>

#include "modbus_io_device.h"
#include "modbus_common.h"


REGISTER_BASIC_PROTOCOL("modbus_io", TModbusIODevice, TAggregatedSlaveId, TRegisterTypes({
    { Modbus::REG_COIL, "coil", "switch", { U8 } },
    { Modbus::REG_DISCRETE, "discrete", "switch", { U8 }, true },
    { Modbus::REG_HOLDING, "holding", "text", { U16 } },
    { Modbus::REG_INPUT, "input", "text", { U16 }, true }
}));

TModbusIODevice::TModbusIODevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusIODevice, TAggregatedSlaveId>>(config, port, protocol)
{
    Shift = (((SlaveId.Secondary - 1) % 4) + 1) * DeviceConfig()->Stride + DeviceConfig()->Shift;
}

const TProtocolInfo & TModbusIODevice::GetProtocolInfo() const
{
    return Modbus::GetProtocolInfo();
}

void TModbusIODevice::Write(const TIRDeviceValueQuery & query)
{
    ModbusRTU::Write(Port(), SlaveId.Primary, query, Shift);
}

void TModbusIODevice::Read(const TIRDeviceQuery & query)
{
    ModbusRTU::Read(Port(), SlaveId.Primary, query, Shift);
}
