#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TEnergomeraIecWithFastReadDevice: public TIEC61107Device
{
public:
    TEnergomeraIecWithFastReadDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    void                      WriteRegister(PRegister reg, uint64_t value);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister>& reg_list,
                                                bool                        enableHoles = true) const override;
    std::list<PRegisterRange> ReadRegisterRange(PRegisterRange range) override;

    static void Register(TSerialDeviceFactory& factory);
};
