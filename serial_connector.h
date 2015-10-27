#pragma once

#include "modbus_client.h"
#include "serial_protocol.h"

class TSerialConnector: public TModbusConnector {
public:
    PModbusContext CreateContext(PAbstractSerialPort port); // for tests
    PModbusContext CreateContext(const TSerialPortSettings& settings);
};
