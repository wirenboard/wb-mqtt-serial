#pragma once

#include "iec_common.h"

class TEnergomeraIecDevice: public TIECDevice
{
public:
    TEnergomeraIecDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    void WriteRegister(PRegister reg, uint64_t value);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    std::list<PRegisterRange> ReadRegisterRange(PRegisterRange range) override;
};
