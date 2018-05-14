#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TS2KDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TS2KDevice>> {
public:
    enum RegisterType
    {
        REG_RELAY = 0,
        REG_RELAY_MODE,
        REG_RELAY_DEFAULT,
        REG_RELAY_DELAY,
    };

    TS2KDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    void Read(const TIRDeviceQuery &) override;
    void Write(const TIRDeviceValueQuery &) override;

private:
    uint8_t CrcS2K(const uint8_t *array, int size);
    static uint8_t CrcTable[];
    uint8_t RelayState[5];
};

typedef std::shared_ptr<TS2KDevice> PS2KDevice;
