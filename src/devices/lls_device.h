#pragma once

#include "serial_config.h"
#include "serial_device.h"

class TLLSDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TLLSDevice(PDeviceConfig config, PProtocol protocol);

    std::chrono::milliseconds GetFrameTimeout(TPort& port) const override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;
    void InvalidateReadCache() override;

private:
    std::unordered_map<uint8_t, std::vector<uint8_t>> CmdResultCache;

    std::vector<uint8_t> ExecCommand(TPort& port, uint8_t cmd);
};
