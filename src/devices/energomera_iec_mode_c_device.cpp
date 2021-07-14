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
    switch (reg.Type)
    {
        case RegisterType::DATE:
        {
            // ww.dd.mm.yy
            auto v(value);
            v.erase(0, 3); // remove day of a week
            v.erase(std::remove(v.begin(), v.end(), '.'), v.end());
            return strtoull(v.c_str(), nullptr, 10);
        }
        case RegisterType::TIME:
        {
            // HH:MM:SS
            auto v(value);
            v.erase(std::remove(v.begin(), v.end(), ':'), v.end());
            return strtoull(v.c_str(), nullptr, 10);
        }
        case RegisterType::DEFAULT:
        {
            if (reg.Format == U64) {
                return strtoull(value.c_str(), nullptr, 10);
            }
            double val = strtod(value.c_str(), nullptr);
            uint64_t res = 0;
            static_assert((sizeof(res) >= sizeof(val)), "Can't fit double into uint64_t");
            memcpy(&res, &val, sizeof(val));
            return res;
        }
        default:
        {
            auto items = WBMQTT::StringSplit(value, ")\r\n(");
            if (items.size() > static_cast<unsigned int>(reg.Type)) {
                double val = strtod(items[reg.Type].c_str(), nullptr);
                uint64_t res = 0;
                static_assert((sizeof(res) >= sizeof(val)), "Can't fit double into uint64_t");
                memcpy(&res, &val, sizeof(val));
                return res;
            }
            throw TSerialDeviceTransientErrorException("malformed response");
        }
    }
}
