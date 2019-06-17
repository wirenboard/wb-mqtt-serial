#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>
#include "serial_device.h"

class TLLSDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TLLSDevice>> {
public:
    TLLSDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);
    virtual void EndPollCycle();

private:
    std::unordered_map<uint8_t, std::vector<uint8_t> > CmdResultCache;
    std::vector<uint8_t> ExecCommand(uint8_t cmd);
};

typedef std::shared_ptr<TLLSDevice> PLLSDevice;
