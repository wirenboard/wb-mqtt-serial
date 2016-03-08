#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TModbusDevice: public TSerialDevice {
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    TModbusDevice(PDeviceConfig config, PAbstractSerialPort port);
    virtual std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list) const;
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);
    void ReadRegisterRange(PRegisterRange range);

private:
    PLibModbusContext Context;
};
