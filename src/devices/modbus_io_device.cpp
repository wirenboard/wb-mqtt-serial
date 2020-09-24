#include <iostream>
#include <stdexcept>

#include "modbus_io_device.h"
#include "modbus_common.h"


REGISTER_BASIC_PROTOCOL("modbus_io", TModbusIODevice, TAggregatedSlaveId, TRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        }));

TModbusIODevice::TModbusIODevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusIODevice, TAggregatedSlaveId>>(config, port, protocol)
{
    Shift = (((SlaveId.Secondary - 1) % 4) + 1) * DeviceConfig()->Stride + DeviceConfig()->Shift;
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

std::list<PRegisterRange> TModbusIODevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), enableHoles);
}

uint64_t TModbusIODevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("modbus extension module: single register reading is not supported");
}

void TModbusIODevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusRTU::WriteRegister(Port(), SlaveId.Primary, reg, value, Shift);
}

void TModbusIODevice::ReadRegisterRange(PRegisterRange range)
{
    ModbusRTU::ReadRegisterRange(Port(), SlaveId.Primary, range, Shift);
}
