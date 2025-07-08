#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TEnergomeraIecWithFastReadDevice: public TIEC61107Device
{
public:
    TEnergomeraIecWithFastReadDevice(PDeviceConfig device_config, PProtocol protocol);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(TPort& port, PRegisterRange range) override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    void PrepareImpl(TPort& port) override;
    void EndSession(TPort& port) override;
};
