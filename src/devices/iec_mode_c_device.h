#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TIecModeCDevice: public TIEC61107ModeCDevice
{
public:
    TIecModeCDevice(PDeviceConfig device_config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

protected:
    void PrepareImpl(TPort& port) override;

private:
    std::string GetParameterRequest(const TRegisterConfig& reg) const override;
    TRegisterValue GetRegisterValue(const TRegisterConfig& reg, const std::string& value) override;
};