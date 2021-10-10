#pragma once

#include "serial_device.h"
#include "serial_config.h"

class TLLSDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TLLSDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void EndPollCycle() override;

    static void Register(TSerialDeviceFactory& factory);

private:
    std::unordered_map<uint8_t, std::vector<uint8_t>> CmdResultCache;

    std::vector<uint8_t> ExecCommand(uint8_t cmd);
};
