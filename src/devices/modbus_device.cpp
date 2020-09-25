#include "modbus_device.h"
#include "modbus_common.h"

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

REGISTER_BASIC_PROTOCOL("modbus", TModbusDevice, ModbusRegisterTypes);

TModbusDevice::TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol), TUInt32SlaveId(config->SlaveId)
{
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, DeviceConfig(), enableHoles);
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    ModbusRTU::WriteRegister(Port(), SlaveId, reg, value);
}

std::list<PRegisterRange> TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    return ModbusRTU::ReadRegisterRange(Port(), SlaveId, range);
}

bool TModbusDevice::WriteSetupRegisters()
{
    return ModbusRTU::WriteSetupRegisters(Port(), SlaveId, SetupItems);
}

class TModbusTCPProtocol: public IProtocol
{
    std::unordered_map<PPort, std::shared_ptr<uint16_t>> TransactionIds;
public:
    TModbusTCPProtocol() : IProtocol("modbus-tcp", ModbusRegisterTypes)
    {}

    PSerialDevice CreateDevice(PDeviceConfig config, PPort port) override
    {
        auto it = TransactionIds.find(port);
        if (it == TransactionIds.end()) {
            std::tie(it, std::ignore) = TransactionIds.insert({port, std::make_shared<uint16_t>(0)});
        }
        PSerialDevice dev = std::make_shared<TModbusTCPDevice>(config, port, this, it->second);
        dev->InitSetupItems();
        return dev;
    }
};

REGISTER_NEW_PROTOCOL(TModbusTCPProtocol);

TModbusTCPDevice::TModbusTCPDevice(PDeviceConfig config, PPort port, PProtocol protocol, std::shared_ptr<uint16_t> transactionId)
    : TSerialDevice(config, port, protocol), TUInt32SlaveId(config->SlaveId),
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
