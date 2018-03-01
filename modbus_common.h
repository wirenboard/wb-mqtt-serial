#pragma once

#include "port.h"
#include "serial_config.h"


namespace Modbus  // modbus protocol common utilities
{
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
        REG_HOLDING_SINGLE,
        REG_HOLDING_MULTI,
    };

    const TProtocolInfo & GetProtocolInfo();
};  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    void Read(const PPort & port, uint8_t slaveId, const TIRDeviceQuery &, int shift = 0);
    void Write(const PPort & port, uint8_t slaveId, const TIRDeviceValueQuery &, int shift = 0);
};  // modbus rtu protocol utilities
