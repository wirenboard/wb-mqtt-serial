#pragma once

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

#include "serial_config.h"
#include "serial_device.h"

class TS2KDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    enum RegisterType
    {
        REG_RELAY = 0,
        REG_RELAY_MODE,
        REG_RELAY_DEFAULT,
        REG_RELAY_DELAY,
    };

    TS2KDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    virtual uint64_t ReadRegister(PRegister reg);
    virtual void WriteRegister(PRegister reg, uint64_t value);

    static void Register(TSerialDeviceFactory& factory);

private:
    uint8_t CrcS2K(const uint8_t* array, int size);

    static uint8_t CrcTable[];
    uint8_t RelayState[5];
};

typedef std::shared_ptr<TS2KDevice> PS2KDevice;
