#include "energomera_iec_mode_c_device.h"
#include "energomera_iec_device.h"

#include "iec_common.h"
#include <memory>
#include <string.h>

namespace
{
    const char* LOG_PREFIX = "[Energomera] ";

    enum RegisterType
    {
        DEFAULT = 0,
        ITEM,
        DATE,
        TIME
    };

    // clang-format off
    const TRegisterTypes RegisterTypes {
        { RegisterType::DEFAULT, "default", "value", Double, true },
        { RegisterType::ITEM,    "item_1",  "value", Double, true },
        { RegisterType::ITEM,    "item_2",  "value", Double, true },
        { RegisterType::ITEM,    "item_3",  "value", Double, true },
        { RegisterType::ITEM,    "item_4",  "value", Double, true },
        { RegisterType::ITEM,    "item_5",  "value", Double, true },
        { RegisterType::DATE,    "date",    "value", U32,    true },
        { RegisterType::TIME,    "time",    "value", U32,    true }
    };
    // clang-format on

    class TEnergomeraIecModeCDeviceRegisterAddressFactory: public TStringRegisterAddressFactory
    {
    public:
        TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t stride,
                                          uint32_t registerByteWidth) const override
        {
            TRegisterDesc res;
            res.Address = std::make_shared<TStringRegisterAddress>(regCfg["address"].asString());
            auto type = regCfg["reg_type"].asString();
            if (WBMQTT::StringStartsWith(type, "item_")) {
                int index = type[5] - '0';
                if (index > 0 && index < 6) {
                    res.DataOffset = index - 1;
                }
            }
            return res;
        }
    };

    class TEnergomeraIecModeCDeviceFactory: public IDeviceFactory
    {
    public:
        TEnergomeraIecModeCDeviceFactory()
            : IDeviceFactory(std::make_unique<TEnergomeraIecModeCDeviceRegisterAddressFactory>(),
                             "#/definitions/iec_mode_c_device",
                             "#/definitions/channel_with_string_address")
        {}

        PSerialDevice CreateDevice(const Json::Value& data,
                                   PDeviceConfig deviceConfig,
                                   PPort port,
                                   PProtocol protocol) const override
        {
            auto dev = std::make_shared<TEnergomeraIecModeCDevice>(deviceConfig, port, protocol);
            dev->InitSetupItems();
            return dev;
        }
    };
}

void TEnergomeraIecModeCDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TIEC61107Protocol("energomera_iec_mode_c", RegisterTypes),
                             new TEnergomeraIecModeCDeviceFactory());
}

TEnergomeraIecModeCDevice::TEnergomeraIecModeCDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TIEC61107ModeCDevice(device_config, port, protocol, LOG_PREFIX, IEC::Calc7BitCrc)
{
    if (DeviceConfig()->Password.empty()) {
        DeviceConfig()->Password = std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00};
    }
}

std::string TEnergomeraIecModeCDevice::GetParameterRequest(const TRegister& reg) const
{
    return reg.GetAddress().ToString();
}

TRegisterValue TEnergomeraIecModeCDevice::GetRegisterValue(const TRegister& reg, const std::string& value)
{
    // Data in response starts from '(' and ends with ")\r\n"
    if (value.size() < 5 || value.front() != '(' || !WBMQTT::StringHasSuffix(value, ")\r\n")) {
        throw TSerialDeviceTransientErrorException("malformed response");
    }
    // Remove '(' and ")\r\n"
    auto v(value.substr(1, value.size() - 4));
    switch (reg.Type) {
        case RegisterType::DATE: {
            // ww.dd.mm.yy
            v.erase(0, 3); // remove day of a week
            v.erase(std::remove(v.begin(), v.end(), '.'), v.end());
            return TRegisterValue{strtoull(v.c_str(), nullptr, 10)};
        }
        case RegisterType::TIME: {
            // HH:MM:SS
            v.erase(std::remove(v.begin(), v.end(), ':'), v.end());
            return TRegisterValue{strtoull(v.c_str(), nullptr, 10)};
        }
        case RegisterType::DEFAULT: {
            if (reg.Format == U64) {
                return TRegisterValue{strtoull(v.c_str(), nullptr, 10)};
            }
            return TRegisterValue{CopyDoubleToUint64(strtod(v.c_str(), nullptr))};
        }
        case RegisterType::ITEM: {
            // An example of a response with a list of values
            // <STX>ET0PE(68.02)<CR><LF>(45.29)<CR><LF>(22.73)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF><ETX>0x07
            // so we have here
            // 68.02)<CR><LF>(45.29)<CR><LF>(22.73)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF>(0.00
            auto items = WBMQTT::StringSplit(v, ")\r\n(");
            if (items.size() > static_cast<unsigned int>(reg.GetDataOffset())) {
                return TRegisterValue{CopyDoubleToUint64(strtod(items[reg.GetDataOffset()].c_str(), nullptr))};
            }
            throw TSerialDeviceTransientErrorException("malformed response");
        }
    }
    throw TSerialDevicePermanentRegisterException("unsupported register type: " + std::to_string(reg.Type));
}
