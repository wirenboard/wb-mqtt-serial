#include "modbus_device.h"
#include "modbus_common.h"

namespace
{
    const TRegisterTypes ModbusRegisterTypes(
        {{Modbus::REG_HOLDING, "holding", "value", RegisterFormat::U16},
         {Modbus::REG_HOLDING_SINGLE, "holding_single", "value", RegisterFormat::U16},
         {Modbus::REG_HOLDING_MULTI, "holding_multi", "value", RegisterFormat::U16},
         {Modbus::REG_COIL, "coil", "switch", RegisterFormat::U8},
         {Modbus::REG_DISCRETE, "discrete", "switch", RegisterFormat::U8, true},
         {Modbus::REG_INPUT, "input", "value", RegisterFormat::U16, true}});

    class TModbusProtocol: public IProtocol
    {
    public:
        TModbusProtocol(const char* name): IProtocol(name, ModbusRegisterTypes)
        {}

        bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override
        {
            return (TUInt32SlaveId(id1) == TUInt32SlaveId(id2));
        }

        bool IsModbus() const override
        {
            return true;
        }
    };
}

void TModbusDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TModbusProtocol("modbus"),
        new TModbusDeviceFactory<TModbusDevice>(std::make_unique<Modbus::TModbusRTUTraitsFactory>()));
    factory.RegisterProtocol(
        new TModbusProtocol("modbus-tcp"),
        new TModbusDeviceFactory<TModbusDevice>(std::make_unique<Modbus::TModbusTCPTraitsFactory>()));
}

TModbusDevice::TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                             PDeviceConfig config,
                             PPort port,
                             PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      ModbusTraits(std::move(modbusTraits))
{
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

PRegisterRange TModbusDevice::CreateRegisterRange(PRegister reg) const
{
    return Modbus::CreateRegisterRange(reg);
}

void TModbusDevice::WriteRegisterImpl(PRegister reg, uint64_t value)
{
    Modbus::WriteRegister(*ModbusTraits, *Port(), SlaveId, *reg, value, ModbusCache);
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    Modbus::ReadRegisterRange(*ModbusTraits, *Port(), SlaveId, range, ModbusCache);
}

void TModbusDevice::WriteSetupRegisters()
{
    Modbus::WriteSetupRegisters(*ModbusTraits, *Port(), SlaveId, SetupItems, ModbusCache);
}
