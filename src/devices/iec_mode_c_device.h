#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TIecModeCDevice: public TIEC61107ModeCDevice
{
public:
    TIecModeCDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

protected:
    void PrepareImpl() override;

private:
    std::string GetParameterRequest(const TRegister& reg) const override;
    TRegisterValue GetRegisterValue(const TRegister& reg, const std::string& value) override;
};