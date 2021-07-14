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
        DEFAULT = 0,
        DATE,
        TIME
    };

    const TRegisterTypes RegisterTypes {
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
        default:
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
    }
}
