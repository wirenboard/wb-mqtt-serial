#include <iostream>
#include <stdexcept>

#include "modbus_device.h"
#include "modbus_common.h"


REGISTER_BASIC_INT_PROTOCOL("modbus", TModbusDevice, TRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        }));

TModbusDevice::TModbusDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>>(config, port, protocol)
{}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), Port()->Debug());
}

uint64_t TModbusDevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("modbus: single register reading is not supported");
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusRTU::WriteRegister(Port(), SlaveId, reg, value);
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    ModbusRTU::ReadRegisterRange(Port(), SlaveId, range);
}
