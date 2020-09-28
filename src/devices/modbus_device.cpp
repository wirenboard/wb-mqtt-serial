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

    const TRegisterTypes ModbusIORegisterTypes({
            { Modbus::REG_COIL, "coil", "switch", U8 },
            { Modbus::REG_DISCRETE, "discrete", "switch", U8, true },
            { Modbus::REG_HOLDING, "holding", "text", U16 },
            { Modbus::REG_INPUT, "input", "text", U16, true }
        });

    int GetSecondaryId(const std::string& fullId)
    {
        try {
            auto delimiter_it = fullId.find(':');
            return std::stoi(fullId.substr(delimiter_it + 1), 0, 0);
        } catch (const std::logic_error &e) {
            throw TSerialDeviceException("slave ID \"" + fullId + "\" is not convertible to modbus_io id");
        }
    }
}

class TModbusProtocol: public IProtocol
{
    bool RTU;
    std::unordered_map<PPort, std::shared_ptr<uint16_t>> TransactionIds;
public:
    TModbusProtocol(const char* name, const TRegisterTypes& registerTypes, bool rtu) : IProtocol(name, registerTypes), RTU(rtu)
    {}

    PSerialDevice CreateDevice(PDeviceConfig config, PPort port) override
    {
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (RTU) {
            traits = std::make_unique<Modbus::TModbusRTUTraits>();
        } else {
            auto it = TransactionIds.find(port);
            if (it == TransactionIds.end()) {
                std::tie(it, std::ignore) = TransactionIds.insert({port, std::make_shared<uint16_t>(0)});
            }

            traits = std::make_unique<Modbus::TModbusTCPTraits>(it->second);
        }

        PSerialDevice dev = std::make_shared<TModbusDevice>(std::move(traits), config, port, this);
        dev->InitSetupItems();
        return dev;
    }

    bool IsSameSlaveId(const std::string& id1, const std::string& id2) const override
    {
        if (TUInt32SlaveId(id1) == TUInt32SlaveId(id2)) {
            if (GetName().find("io") != std::string::npos) {
                return GetSecondaryId(id1) == GetSecondaryId(id2);
            }
            return true;
        }
        return false;
    }
};

TProtocolRegistrator reg__TModbusProtocol(new TModbusProtocol("modbus",     ModbusRegisterTypes, true));
TProtocolRegistrator reg__TModbusTCPProtocol(new TModbusProtocol("modbus-tcp", ModbusRegisterTypes, false));

TProtocolRegistrator reg__TModbusIOProtocol(new TModbusProtocol("modbus_io",     ModbusIORegisterTypes, true));
TProtocolRegistrator reg__TModbusIOTCPProtocol(new TModbusProtocol("modbus_io-tcp", ModbusIORegisterTypes, false));

TModbusDevice::TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits, PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol), TUInt32SlaveId(config->SlaveId), ModbusTraits(std::move(modbusTraits))
{
    if (protocol->GetName().find("io") != std::string::npos) {
        auto SecondaryId = GetSecondaryId(config->SlaveId);
        Shift = (((SecondaryId - 1) % 4) + 1) * DeviceConfig()->Stride + DeviceConfig()->Shift;
    }
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles) const
{
    return Modbus::SplitRegisterList(reg_list, *DeviceConfig(), enableHoles);
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    Modbus::WriteRegister(*ModbusTraits, *Port(), SlaveId, *reg, value, Shift);
}

std::list<PRegisterRange> TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    return Modbus::ReadRegisterRange(*ModbusTraits, *Port(), SlaveId, range, Shift);
}

bool TModbusDevice::WriteSetupRegisters()
{
    return Modbus::WriteSetupRegisters(*ModbusTraits, *Port(), SlaveId, SetupItems, Shift);
}
