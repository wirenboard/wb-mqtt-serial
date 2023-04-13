#include "modbus_device.h"
#include "log.h"
#include "modbus_common.h"
#include "modbus_ext_common.h"
#include "serial_client.h"

#define LOG(logger) logger.Log() << "[modbus] "

namespace
{
    const TRegisterTypes ModbusRegisterTypes({{Modbus::REG_HOLDING, "holding", "value", U16},
                                              {Modbus::REG_HOLDING_SINGLE, "holding_single", "value", U16},
                                              {Modbus::REG_HOLDING_MULTI, "holding_multi", "value", U16},
                                              {Modbus::REG_COIL, "coil", "switch", U8},
                                              {Modbus::REG_DISCRETE, "discrete", "switch", U8, true},
                                              {Modbus::REG_INPUT, "input", "value", U16, true}});
#if 0
    ModbusExt::TEventRegisterType ToEventRegisterType(const Modbus::RegisterType regType)
    {
        switch (regType) {
            case Modbus::REG_COIL:
                return ModbusExt::TEventRegisterType::COIL;
            case Modbus::REG_DISCRETE:
                return ModbusExt::TEventRegisterType::DISCRETE;
            case Modbus::REG_HOLDING:
            case Modbus::REG_HOLDING_SINGLE:
            case Modbus::REG_HOLDING_MULTI:
                return ModbusExt::TEventRegisterType::HOLDING;
            case Modbus::REG_INPUT:
                return ModbusExt::TEventRegisterType::INPUT;
            default:
                throw std::runtime_error("unsupported register type");
        }
    }
#endif
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
    config.CommonConfig->FrameTimeout = std::max(config.CommonConfig->FrameTimeout, port->GetSendTime(3.5));
}

PRegisterRange TModbusDevice::CreateRegisterRange() const
{
    return Modbus::CreateRegisterRange(ResponseTime.GetValue());
}

void TModbusDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& value)
{
    Modbus::WriteRegister(*ModbusTraits, *Port(), SlaveId, *reg, value, ModbusCache);
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
    Modbus::WriteSetupRegisters(*ModbusTraits, *Port(), SlaveId, SetupItems, ModbusCache);
#if 0
    std::chrono::milliseconds responseTimeout = std::chrono::milliseconds(100);
    std::chrono::milliseconds frameTimeout = std::chrono::milliseconds(100);
    ModbusExt::TEventsEnabler ev(SlaveId,
                                 *Port(),
                                 responseTimeout,
                                 frameTimeout,
                                 std::bind(&TModbusDevice::OnEnabledEvent,
                                           this,
                                           std::placeholders::_1,
                                           std::placeholders::_2,
                                           std::placeholders::_3));

    try {
        for (const auto& ch: DeviceConfig()->DeviceChannelConfigs) {
            for (const auto& reg: ch->RegisterConfigs) {
                if (reg->SporadicMode == TRegisterConfig::TSporadicMode::UNKNOWN) {
                    auto addr = GetUint32RegisterAddress(reg->GetAddress());
                    auto type = ToEventRegisterType(static_cast<Modbus::RegisterType>(reg->Type));
                    ev.AddRegister(addr, type, true);
                }
            }
        }
        LOG(Debug) << "Try to enable events on " << ToString();
        ev.SendRequest();
    } catch (const std::exception& e) {
        LOG(Warn) << "Failed to enable events on " << ToString() << ": " << e.what();
    }
#endif
}

void TModbusDevice::OnEnabledEvent(uint16_t addr, uint8_t type, uint8_t res)
{
    auto serialClient = SerialClient.lock();
    if (!serialClient) {
        LOG(Error) << "Serial client is not set";
        return;
    }

    auto reg = serialClient->FindRegister(static_cast<uint8_t>(SlaveId), addr);
    if (!reg) {
        LOG(Error) << "Register not found: " << reg->ToString();
        return;
    }

    if (res == 1) {
        reg->SporadicMode = TRegisterConfig::TSporadicMode::ENABLED;
        LOG(Info) << "Events are enabled for " << reg->ToString();
    } else if (res == 0) {
        reg->SporadicMode = TRegisterConfig::TSporadicMode::DISABLED;
        LOG(Info) << "Events are disabled for " << reg->ToString();
    } else {
        LOG(Error) << "Error on enabling events for " << reg->ToString() << ", res: " << static_cast<int>(res);
    }
}