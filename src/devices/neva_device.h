#pragma once

#include "iec_common.h"
#include "serial_config.h"

class TNevaDevice: public TIECDevice
{
public:
    TNevaDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void EndPollCycle() override;
    void EndSession() override;
    void Prepare() override;

    static void Register(TSerialDeviceFactory& factory);

private:
    std::unordered_map<uint32_t, std::vector<double> > CmdResultCache;
};
