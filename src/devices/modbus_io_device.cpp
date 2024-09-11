#include "modbus_io_device.h"

namespace
{
    const TRegisterTypes ModbusIORegisterTypes({{Modbus::REG_HOLDING, "holding", "value", U16},
                                                {Modbus::REG_COIL, "coil", "switch", U8},
                                                {Modbus::REG_DISCRETE, "discrete", "switch", U8, true},
                                                {Modbus::REG_INPUT, "input", "value", U16, true}});

    int GetSecondaryId(const std::string& fullId)
    {
        try {
            auto delimiter_it = fullId.find(':');
            return std::stoi(fullId.substr(delimiter_it + 1), 0, 0);
        } catch (const std::logic_error& e) {
            throw TSerialDeviceException("slave ID \"" + fullId + "\" is not convertible to modbus_io id");
        }
    }

    class TModbusIOProtocol: public IProtocol
    {
        std::unique_ptr<Modbus::IModbusTraitsFactory> ModbusTraitsFactory;

    public:
        TModbusIOProtocol(const char* name): IProtocol(name, ModbusIORegisterTypes)
        {}

        bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override
        {
            if (TUInt32SlaveId(id1) == TUInt32SlaveId(id2)) {
                return GetSecondaryId(id1) == GetSecondaryId(id2);
            }
            return false;
        }

        bool IsModbus() const override
        {
            return true;
        }
    };
}

void TModbusIODevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TModbusIOProtocol("modbus_io"),
        new TModbusDeviceFactory<TModbusIODevice>(std::make_unique<Modbus::TModbusRTUTraitsFactory>()));
    factory.RegisterProtocol(
        new TModbusIOProtocol("modbus_io-tcp"),
        new TModbusDeviceFactory<TModbusIODevice>(std::make_unique<Modbus::TModbusTCPTraitsFactory>()));
}

TModbusIODevice::TModbusIODevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                                 const TModbusDeviceConfig& config,
                                 PPort port,
                                 PProtocol protocol)
    : TSerialDevice(config.CommonConfig, port, protocol),
      TUInt32SlaveId(config.CommonConfig->SlaveId),
      ModbusTraits(std::move(modbusTraits)),
      ResponseTime(std::chrono::milliseconds::zero())
{
    auto SecondaryId = GetSecondaryId(config.CommonConfig->SlaveId);
    Shift = (((SecondaryId - 1) % 4) + 1) * DeviceConfig()->Stride + DeviceConfig()->Shift;
    config.CommonConfig->FrameTimeout = std::max(
        config.CommonConfig->FrameTimeout,
        std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES)));
}

PRegisterRange TModbusIODevice::CreateRegisterRange() const
{
    return Modbus::CreateRegisterRange(ResponseTime.GetValue());
}

void TModbusIODevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& value)
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
                          DeviceConfig()->FrameTimeout,
                          Shift);
}

void TModbusIODevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
    if (!modbus_range) {
        throw std::runtime_error("modbus range expected");
    }
    Modbus::ReadRegisterRange(*ModbusTraits, *Port(), SlaveId, *modbus_range, ModbusCache, Shift);
    ResponseTime.AddValue(modbus_range->GetResponseTime());
}

void TModbusIODevice::WriteSetupRegisters()
{
    Modbus::EnableWbContinuousRead(shared_from_this(), *ModbusTraits, *Port(), SlaveId, ModbusCache);
    Modbus::WriteSetupRegisters(*ModbusTraits,
                                *Port(),
                                SlaveId,
                                0,
                                SetupItems,
                                ModbusCache,
                                DeviceConfig()->RequestDelay,
                                DeviceConfig()->ResponseTimeout,
                                DeviceConfig()->FrameTimeout,
                                Shift);
}
