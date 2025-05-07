#pragma once

#include "serial_config.h"

class TEnergomeraCeDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TEnergomeraCeDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;
};

typedef std::shared_ptr<TEnergomeraCeDevice> PEnergomeraCeDevice;
