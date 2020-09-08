#include <iostream>
#include <stdexcept>

#include "modbus_io_device.h"
#include "modbus_common.h"

#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial device] "

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

void TModbusIODevice::SetReadError(PRegisterRange range)
{
    ModbusRTU::SetReadError(range);
}

bool TModbusIODevice::WriteSetupRegisters()
{
    for (const auto& item : SetupItems) {
        try {
            try {
                LOG(Info) << "Init: " << item->Name 
                        << ": setup register " << item->Register->ToString()
                        << " <-- " << item->Value;
                WriteRegister(item->Register, item->Value);
            } catch (const TModbusException& modbusException) {
                if (modbusException.code() != Modbus::ERR_ILLEGAL_DATA_ADDRESS) {
                    throw;
                }
            }
        } catch (const TSerialDeviceException& e) {
            LOG(Warn) << "Register " << item->Register->ToString()
                      << " setup failed: " << e.what();
            return false;
        }
    }
    return true;
}
