#pragma once

#include "serial_config.h"
#include "serial_device.h"

class TLLSDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TLLSDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

protected:
    TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;
    void InvalidateReadCache() override;

private:
    std::unordered_map<uint8_t, std::vector<uint8_t>> CmdResultCache;

    std::vector<uint8_t> ExecCommand(uint8_t cmd);
};
