#include "modbus_device.h"
#include "modbus_common.h"

#include <iostream>
#include <stdexcept>


REGISTER_BASIC_INT_PROTOCOL("modbus", TModbusDevice, TRegisterTypes({
    { Modbus::REG_COIL, "coil", "switch", { U8 } },
    { Modbus::REG_DISCRETE, "discrete", "switch", { U8 }, true },
    { Modbus::REG_HOLDING, "holding", "text", { U16 } },
    { Modbus::REG_HOLDING_SINGLE, "holding_single", "text", { U16 } },
    { Modbus::REG_HOLDING_MULTI, "holding_multi", "text", { U16 } },
    { Modbus::REG_INPUT, "input", "text", { U16 }, true }
}));

TModbusDevice::TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>>(config, port, protocol)
{}

const TProtocolInfo & TModbusDevice::GetProtocolInfo() const
{
    return Modbus::GetProtocolInfo();
}

void TModbusDevice::Read(const TIRDeviceQuery & query)
{
    ModbusRTU::Read(Port(), SlaveId, query);
}

void TModbusDevice::Write(const TIRDeviceValueQuery & query)
{
    ModbusRTU::Write(Port(), SlaveId, query);
}
