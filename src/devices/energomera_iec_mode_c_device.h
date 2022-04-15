#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TEnergomeraIecModeCDevice: public TIEC61107ModeCDevice
{
public:
    TEnergomeraIecModeCDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

private:
    std::string GetParameterRequest(const TRegister& reg) const override;
    Register::TValue GetRegisterValue(const TRegister& reg, const std::string& value) override;
};