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
    public:
        TModbusProtocol(const char* name) : IProtocol(name, ModbusRegisterTypes)
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

    class TModbusDeviceFactory: public IDeviceFactory
    {
        std::unique_ptr<Modbus::IModbusTraitsFactory> ModbusTraitsFactory;
    public:
        TModbusDeviceFactory(std::unique_ptr<Modbus::IModbusTraitsFactory> modbusTraitsFactory)
            : ModbusTraitsFactory(std::move(modbusTraitsFactory))
        {}

        PSerialDevice CreateDevice(const Json::Value& deviceData,
                                   const Json::Value& deviceTemplate,
                                   PProtocol          protocol,
                                   const std::string& defaultId,
                                   PPortConfig        portConfig) const override
        {
            TDeviceConfigLoadParams params;
            params.BaseRegisterAddress = std::make_unique<TUint32RegisterAddress>(0);
            params.DefaultId           = defaultId;
            params.DefaultPollInterval = portConfig->PollInterval;
            params.DefaultRequestDelay = portConfig->RequestDelay;
            params.PortResponseTimeout = portConfig->ResponseTimeout;
            auto deviceConfig = LoadBaseDeviceConfig(deviceData, deviceTemplate, protocol, *this, params);

            PSerialDevice dev = std::make_shared<TModbusDevice>(ModbusTraitsFactory->GetModbusTraits(portConfig->Port), deviceConfig, portConfig->Port, protocol);
            dev->InitSetupItems();
            return dev;
        }

        TRegisterDesc LoadRegisterAddress(const Json::Value&      regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t                stride,
                                          uint32_t                registerByteWidth) const override
        {
            auto addr = LoadRegisterBitsAddress(regCfg);
            TRegisterDesc res;
            res.BitOffset = addr.BitOffset;
            res.BitWidth = addr.BitWidth;
            res.Address = std::shared_ptr<IRegisterAddress>(deviceBaseAddress.CalcNewAddress(addr.Address, stride, registerByteWidth, 2));
            return res;
        }
    };
}

void TModbusDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TModbusProtocol("modbus"), 
                             new TModbusDeviceFactory(std::make_unique<Modbus::TModbusRTUTraitsFactory>()));
    factory.RegisterProtocol(new TModbusProtocol("modbus-tcp"), 
                             new TModbusDeviceFactory(std::make_unique<Modbus::TModbusTCPTraitsFactory>()));
}

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
