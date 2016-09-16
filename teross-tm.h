#pragma once

#include <tuple>
#include <memory>
#include "serial_device.h"
#include <stdint.h>

#include <wbmqtt/utils.h>

class TTerossTMDevice : public TSerialDevice 
{
public:
    enum TRegisterType {
        REG_DEFAULT,
        REG_STATE,
        REG_VERSION
    };

    TTerossTMDevice(PDeviceConfig device_config, PAbstractSerialPort port);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);

private:
 
    uint16_t CalculateChecksum(const uint8_t *buffer, int size);

    uint64_t ReadDataRegister(PRegister reg);
    uint64_t ReadStateRegister(PRegister reg);
    uint64_t ReadVersionRegister(PRegister reg);

    void WriteVersionRequest(uint32_t address);
    void WriteDataRequest(uint32_t address, uint8_t channel, uint8_t param_code);
    void WriteStateRequest(uint32_t address, uint8_t channel);

    TMaybe<uint16_t> Version;
};
