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

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;

protected:
    void WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value) override;

private:
    uint8_t CrcS2K(const uint8_t* array, int size);

    static uint8_t CrcTable[];
    uint8_t RelayState[5];
};

typedef std::shared_ptr<TS2KDevice> PS2KDevice;
