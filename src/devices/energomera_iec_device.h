#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TEnergomeraIecWithFastReadDevice: public TIEC61107Device
{
public:
    TEnergomeraIecWithFastReadDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister>& reg_list,
                                                std::chrono::milliseconds pollLimit) const override;
    void ReadRegisterRange(PRegisterRange range) override;

    static void Register(TSerialDeviceFactory& factory);
};
