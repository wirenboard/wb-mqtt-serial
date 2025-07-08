#include "iec_mode_c_device.h"

#include "iec_common.h"
#include <memory>
#include <string.h>

namespace
{
    const char* LOG_PREFIX = "[IEC61107] ";

    enum RegisterType
    {
        DEFAULT = 0
    };

    const TRegisterTypes RegisterTypes{{RegisterType::DEFAULT, "default", "value", Double, true}};

    TRegisterValue GetValue(const TRegisterConfig& reg, const std::string& value)
    {
        // (019132.530*kWh)
        auto startPos = value.find('(');
        if (startPos == std::string::npos) {
            throw TSerialDeviceTransientErrorException("malformed response: " + value);
        }
        if (reg.Format == U8 || reg.Format == U16 || reg.Format == U24 || reg.Format == U32 || reg.Format == U64) {
            return TRegisterValue{strtoull(value.c_str() + startPos + 1, nullptr, 10)};
        }
        if (reg.Format == S8 || reg.Format == S16 || reg.Format == S24 || reg.Format == S32 || reg.Format == S64) {
            return TRegisterValue{static_cast<uint64_t>(strtoll(value.c_str() + startPos + 1, nullptr, 10))};
        }
        return TRegisterValue{CopyDoubleToUint64(strtod(value.c_str() + startPos + 1, nullptr))};
    }
}

void TIecModeCDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TIEC61107Protocol("iec_mode_c", RegisterTypes),
                             new TBasicDeviceFactory<TIecModeCDevice, TStringRegisterAddressFactory>(
                                 "#/definitions/iec_mode_c_device",
                                 "#/definitions/channel_with_string_address"));
}

TIecModeCDevice::TIecModeCDevice(PDeviceConfig device_config, PProtocol protocol)
    : TIEC61107ModeCDevice(device_config, protocol, LOG_PREFIX, IEC::CalcXorCRC)
{
    SetDesiredBaudRate(9600);
    SetDefaultBaudRate(300);
    SetReadCommand(IEC::FormattedReadCommand);
}

std::string TIecModeCDevice::GetParameterRequest(const TRegisterConfig& reg) const
{
    return reg.GetAddress().ToString();
}

TRegisterValue TIecModeCDevice::GetRegisterValue(const TRegisterConfig& reg, const std::string& value)
{
    switch (reg.Type) {
        case RegisterType::DEFAULT:
            return GetValue(reg, value);
    }
    throw TSerialDevicePermanentRegisterException("unsupported register type: " + std::to_string(reg.Type));
}

void TIecModeCDevice::PrepareImpl(TPort& port)
{
    // Some energy meters require a period of silence before open session request
    // The timeout is not defined in standard and meter's documentation
    // Closest similar timeout is minimal time between session start request and meter's response
    // It is 200ms. So use it as timeout before session start
    port.SleepSinceLastInteraction(std::chrono::milliseconds(200));
    TIEC61107ModeCDevice::PrepareImpl(port);
}
