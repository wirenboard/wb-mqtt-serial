#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TNevaDevice: public TIEC61107ModeCDevice
{
public:
    TNevaDevice(PDeviceConfig device_config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

private:
    std::string GetParameterRequest(const TRegisterConfig& reg) const override;
    TRegisterValue GetRegisterValue(const TRegisterConfig& reg, const std::string& value) override;
};
