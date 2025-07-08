#pragma once

#include "serial_config.h"

class TEnergomeraCeDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TEnergomeraCeDevice(PDeviceConfig config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;
};

typedef std::shared_ptr<TEnergomeraCeDevice> PEnergomeraCeDevice;
