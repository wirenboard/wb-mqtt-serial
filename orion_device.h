#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TOrionDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TOrionDevice>> {
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_RELAY = 0,
    };

    TOrionDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);

private:
    uint8_t OrionCrc(const uint8_t *array, int size);
static uint8_t crc_table[];
};

typedef std::shared_ptr<TOrionDevice> POrionDevice;
