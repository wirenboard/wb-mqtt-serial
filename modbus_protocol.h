#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_protocol.h"

class TModbusProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    TModbusProtocol(PDeviceConfig, PAbstractSerialPort port);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);

private:
    static bool IsSingleBit(int type) {
        return type == REG_COIL || type == REG_DISCRETE;
    }
    PLibModbusContext Context;
};
