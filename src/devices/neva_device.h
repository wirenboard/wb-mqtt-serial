#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TNevaDevice: public TIEC61107ModeCDevice
{
public:
    TNevaDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

private:
    std::string GetParameterRequest(const TRegister& reg) const override;
    TChannelValue GetRegisterValue(const TRegister& reg, const std::string& value) override;
};
