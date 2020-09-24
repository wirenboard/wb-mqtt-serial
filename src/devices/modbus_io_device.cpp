#include <iostream>
#include <stdexcept>

#include "modbus_io_device.h"
#include "modbus_common.h"


REGISTER_BASIC_PROTOCOL("modbus_io", TModbusIODevice, TRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        }));

TModbusIODevice::TModbusIODevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol), TAggregatedSlaveId(config->SlaveId)
{
    Shift = (((SecondaryId - 1) % 4) + 1) * DeviceConfig()->Stride + DeviceConfig()->Shift;
}

std::list<PRegisterRange> TModbusIODevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), enableHoles);
}

void TModbusIODevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusRTU::WriteRegister(Port(), PrimaryId, reg, value, Shift);
}

void TModbusIODevice::ReadRegisterRange(PRegisterRange range)
{
    ModbusRTU::ReadRegisterRange(Port(), PrimaryId, range, Shift);
}
