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

    class TModbusProtocol: public IProtocol
    {
        std::unique_ptr<Modbus::IModbusTraitsFactory> ModbusTraitsFactory;
    public:
        TModbusProtocol(const char* name, std::unique_ptr<Modbus::IModbusTraitsFactory> modbusTraitsFactory) 
            : IProtocol(name, ModbusRegisterTypes),
            ModbusTraitsFactory(std::move(modbusTraitsFactory))
        {}

        PSerialDevice CreateDevice(PDeviceConfig config, PPort port) override
        {
            PSerialDevice dev = std::make_shared<TModbusDevice>(ModbusTraitsFactory->GetModbusTraits(port), config, port, this);
            dev->InitSetupItems();
            return dev;
        }

        bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override
        {
            return (TUInt32SlaveId(id1) == TUInt32SlaveId(id2));
        }

        bool IsModbus() const override
        {
            return true;
        }

        bool SupportsBroadcast() const override
        {
            return false;
        }
    };
}

TProtocolRegistrator reg__TModbusProtocol(new TModbusProtocol("modbus",        std::make_unique<Modbus::TModbusRTUTraitsFactory>()));
TProtocolRegistrator reg__TModbusTCPProtocol(new TModbusProtocol("modbus-tcp", std::make_unique<Modbus::TModbusTCPTraitsFactory>()));

TModbusDevice::TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits, PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol), TUInt32SlaveId(config->SlaveId), ModbusTraits(std::move(modbusTraits))
{
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, *DeviceConfig(), enableHoles);
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    Modbus::WriteRegister(*ModbusTraits, *Port(), SlaveId, *reg, value);
}

std::list<PRegisterRange> TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    return Modbus::ReadRegisterRange(*ModbusTraits, *Port(), SlaveId, range);
}

bool TModbusDevice::WriteSetupRegisters()
{
    return Modbus::WriteSetupRegisters(*ModbusTraits, *Port(), SlaveId, SetupItems);
}
