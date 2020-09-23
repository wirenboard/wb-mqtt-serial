#include "modbus_device.h"
#include "modbus_common.h"

#include <iostream>
#include <stdexcept>

namespace 
{
    const TRegisterTypes ModbusRegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_HOLDING_SINGLE, "holding_single", "text", U16 },
            { Modbus::REG_HOLDING_MULTI, "holding_multi", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        });
}

REGISTER_BASIC_INT_PROTOCOL("modbus", TModbusDevice, ModbusRegisterTypes);
REGISTER_BASIC_INT_PROTOCOL("modbus-tcp", TModbusTCPDevice, ModbusRegisterTypes);

TModbusDevice::TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>>(config, port, protocol)
{}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), enableHoles);
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusRTU::WriteRegister(Port(), SlaveId, reg, value);
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    ModbusRTU::ReadRegisterRange(Port(), SlaveId, range);
}

TModbusTCPDevice::TModbusTCPDevice(PDeviceConfig config, PPort port, PProtocol protocol, std::shared_ptr<uint16_t> transactionId)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusTCPDevice>>(config, port, protocol).
      TransactionId(transactionId)
{}

std::list<PRegisterRange> TModbusTCPDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), enableHoles);
}

void TModbusTCPDevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusTCP::WriteRegister(Port(), SlaveId, reg, value, TransactionId.get());
}

void TModbusTCPDevice::ReadRegisterRange(PRegisterRange range)
{
    ModbusTCP::ReadRegisterRange(Port(), SlaveId, range, TransactionId.get());
}
