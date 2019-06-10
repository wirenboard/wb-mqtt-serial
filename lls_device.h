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

private:
};

typedef std::shared_ptr<TLLSDevice> PLLSDevice;
