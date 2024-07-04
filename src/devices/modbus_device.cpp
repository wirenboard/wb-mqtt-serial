#include "modbus_device.h"
#include "modbus_common.h"

#define LOG(logger) logger.Log() << "[modbus] "

namespace
{
    const TRegisterTypes ModbusRegisterTypes({{Modbus::REG_HOLDING, "holding", "value", U16},
                                              {Modbus::REG_HOLDING_SINGLE, "holding_single", "value", U16},
                                              {Modbus::REG_HOLDING_MULTI, "holding_multi", "value", U16},
                                              {Modbus::REG_COIL, "coil", "switch", U8},
                                              {Modbus::REG_DISCRETE, "discrete", "switch", U8, true},
                                              {Modbus::REG_INPUT, "input", "value", U16, true}});

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
                             const TModbusDeviceConfig& config,
                             PPort port,
                             PProtocol protocol)
    : TSerialDevice(config.CommonConfig, port, protocol),
      TUInt32SlaveId(config.CommonConfig->SlaveId),
      ModbusTraits(std::move(modbusTraits)),
      ResponseTime(std::chrono::milliseconds::zero()),
      EnableWbContinuousRead(config.EnableWbContinuousRead)
{
    config.CommonConfig->FrameTimeout = std::max(
        config.CommonConfig->FrameTimeout,
        std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES)));
}

PRegisterRange TModbusDevice::CreateRegisterRange() const
{
    return Modbus::CreateRegisterRange(ResponseTime.GetValue());
}

void TModbusDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& value)
{
    Modbus::WriteRegister(*ModbusTraits,
                          *Port(),
                          SlaveId,
                          0,
                          *reg,
                          value,
                          ModbusCache,
                          DeviceConfig()->RequestDelay,
                          DeviceConfig()->ResponseTimeout,
                          DeviceConfig()->FrameTimeout);
}

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
    if (!modbus_range) {
        throw std::runtime_error("modbus range expected");
    }
    Modbus::ReadRegisterRange(*ModbusTraits, *Port(), SlaveId, *modbus_range, ModbusCache);
    ResponseTime.AddValue(modbus_range->GetResponseTime());
}

void TModbusDevice::WriteSetupRegisters()
{
    if (EnableWbContinuousRead) {
        Modbus::EnableWbContinuousRead(shared_from_this(), *ModbusTraits, *Port(), SlaveId, ModbusCache);
    }
    Modbus::WriteSetupRegisters(*ModbusTraits,
                                *Port(),
                                SlaveId,
                                0,
                                SetupItems,
                                ModbusCache,
                                DeviceConfig()->RequestDelay,
                                DeviceConfig()->ResponseTimeout,
                                DeviceConfig()->FrameTimeout);
}
