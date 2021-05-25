#include "energomera_iec_mode_c_device.h"
#include "energomera_iec_device.h"

#include <string.h>
#include <memory>
#include "iec_common.h"

namespace
{
    const char* LOG_PREFIX = "[Energomera] ";

    enum RegisterType
    {
        ITEM_1 = 0,
        ITEM_2,
        ITEM_3,
        ITEM_4,
        ITEM_5,
        DEFAULT,
        DATE,
        TIME
    };

    const TRegisterTypes RegisterTypes {
        { RegisterType::ITEM_1,  "item_1",  "value", Double, true },
        { RegisterType::ITEM_2,  "item_2",  "value", Double, true },
        { RegisterType::ITEM_3,  "item_3",  "value", Double, true },
        { RegisterType::ITEM_4,  "item_4",  "value", Double, true },
        { RegisterType::ITEM_5,  "item_5",  "value", Double, true },
        { RegisterType::DEFAULT, "default", "value", Double, true },
        { RegisterType::DATE,    "date",    "value", U32,    true },
        { RegisterType::TIME,    "time",    "value", U32,    true }
    };

    class TEnergomeraIecModeCDeviceFactory: public IDeviceFactory
    {
    public:
        TEnergomeraIecModeCDeviceFactory()
            : IDeviceFactory(std::make_unique<TStringRegisterAddressFactory>(),
                             "#/definitions/simple_device_with_broadcast",
                             "#/definitions/channel_with_string_address")
        {}

        PSerialDevice CreateDevice(const Json::Value& data,
                                   PDeviceConfig      deviceConfig,
                                   PPort              port,
                                   PProtocol          protocol) const override
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
    : TIEC61107ModeCDevice(device_config, port, protocol, LOG_PREFIX, IEC::Get7BitSum)
{}

std::string TEnergomeraIecModeCDevice::GetParameterRequest(const TRegister& reg) const
{
    return reg.GetAddress().ToString();
}

uint64_t TEnergomeraIecModeCDevice::GetRegisterValue(const TRegister& reg, const std::string& value)
{
    // Data in response starts from '(' and ends with ")\r\n"
    if (value.size() < 5 || value.front() != '(' || !WBMQTT::StringHasSuffix(value, ")\r\n") ) {
        throw TSerialDeviceTransientErrorException("malformed response");
    }
    // Remove '(' and ")\r\n"
    auto v(value.substr(1, value.size() - 4));
    switch (reg.Type)
    {
        case RegisterType::DATE:
        {
            // ww.dd.mm.yy
            v.erase(0, 3); // remove day of a week
            v.erase(std::remove(v.begin(), v.end(), '.'), v.end());
            return strtoull(v.c_str(), nullptr, 10);
        }
        case RegisterType::TIME:
        {
            // HH:MM:SS
            v.erase(std::remove(v.begin(), v.end(), ':'), v.end());
            return strtoull(v.c_str(), nullptr, 10);
        }
        case RegisterType::DEFAULT:
        {
            if (reg.Format == U64) {
                return strtoull(v.c_str(), nullptr, 10);
            }
            return CopyDoubleToUint64(strtod(v.c_str(), nullptr));
        }
        default:
        {
            // An example of a response with a list of values
            // <STX>ET0PE(68.02)<CR><LF>(45.29)<CR><LF>(22.73)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF><ETX>0x07
            // so we have here
            // 68.02)<CR><LF>(45.29)<CR><LF>(22.73)<CR><LF>(0.00)<CR><LF>(0.00)<CR><LF>(0.00
            auto items = WBMQTT::StringSplit(v, ")\r\n(");
            if (items.size() > static_cast<unsigned int>(reg.Type)) {
                return CopyDoubleToUint64(strtod(items[reg.Type].c_str(), nullptr));
            }
            throw TSerialDeviceTransientErrorException("malformed response");
        }
    }
}
