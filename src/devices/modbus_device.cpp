#include "modbus_device.h"
#include "modbus_common.h"

namespace
{
    const TRegisterTypes ModbusRegisterTypes({{Modbus::REG_HOLDING, "holding", "value", U16},
                                              {Modbus::REG_HOLDING_SINGLE, "holding_single", "value", U16},
                                              {Modbus::REG_HOLDING_MULTI, "holding_multi", "value", U16},
                                              {Modbus::REG_COIL, "coil", "switch", U8},
                                              {Modbus::REG_DISCRETE, "discrete", "switch", U8, true},
                                              {Modbus::REG_INPUT, "input", "value", U16, true},
                                              {Modbus::REG_INPUT, "press_counter", "value", U16, true}});

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
                             PProtocol protocol)
    : TSerialDevice(config.CommonConfig, protocol),
      TUInt32SlaveId(config.CommonConfig->SlaveId),
      ModbusTraits(std::move(modbusTraits)),
      ResponseTime(std::chrono::milliseconds::zero()),
      EnableWbContinuousRead(config.EnableWbContinuousRead),
      ContinuousReadEnabled(false)
{}

bool TModbusDevice::GetForceFrameTimeout()
{
    return ModbusTraits->GetForceFrameTimeout();
}

bool TModbusDevice::GetContinuousReadEnabled()
{
    return ContinuousReadEnabled;
}

PRegisterRange TModbusDevice::CreateRegisterRange() const
{
    return Modbus::CreateRegisterRange(ResponseTime.GetValue());
}

void TModbusDevice::PrepareImpl(TPort& port)
{
    TSerialDevice::PrepareImpl(port);
    if (GetConnectionState() != TDeviceConnectionState::CONNECTED) {
        if (EnableWbContinuousRead) {
            ContinuousReadEnabled =
                Modbus::EnableWbContinuousRead(shared_from_this(), *ModbusTraits, port, SlaveId, ModbusCache);
        }
        if (!IsWbDevice()) {
            return;
        }
        SetWbFwVersion(Modbus::ReadWbFwVersion(shared_from_this(), *ModbusTraits, port, SlaveId));
        if (GetWbFwVersion().empty()) {
            return;
        }
        for (const auto& reg: GetRegisters()) {
            const auto& fw = reg->GetConfig()->FwVersion;
            if (!fw.empty() && util::CompareVersionStrings(fw, GetWbFwVersion()) > 0) {
                reg->SetError(TRegister::TError::ReadError);
                reg->SetSupported(false);
                reg->ExcludeFromPolling();
            } else {
                reg->ClearError(TRegister::TError::ReadError);
                reg->SetSupported(true);
                reg->IncludeInPolling();
            }
        }
    }
}

void TModbusDevice::WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& value)
{
    Modbus::WriteRegister(*ModbusTraits,
                          port,
                          SlaveId,
                          reg,
                          value,
                          ModbusCache,
                          DeviceConfig()->RequestDelay,
                          GetResponseTimeout(port),
                          GetFrameTimeout(port));
}

void TModbusDevice::ReadRegisterRange(TPort& port, PRegisterRange range, bool breakOnError)
{
    auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
    if (!modbus_range) {
        throw std::runtime_error("modbus range expected");
    }
    Modbus::ReadRegisterRange(*ModbusTraits, port, SlaveId, *modbus_range, ModbusCache, breakOnError);
    ResponseTime.AddValue(modbus_range->GetResponseTime());
}

void TModbusDevice::WriteSetupRegisters(TPort& port, const TDeviceSetupItems& setupItems, bool breakOnError)
{
    Modbus::WriteSetupRegisters(*ModbusTraits,
                                port,
                                SlaveId,
                                setupItems,
                                ModbusCache,
                                DeviceConfig()->RequestDelay,
                                GetResponseTimeout(port),
                                GetFrameTimeout(port),
                                breakOnError);
}

std::chrono::milliseconds TModbusDevice::GetFrameTimeout(TPort& port) const
{
    return std::max(
        DeviceConfig()->FrameTimeout,
        std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES)));
}
