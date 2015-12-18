#pragma once

#include <functional>
#include "modbus_client.h"
#include "serial_protocol.h"

class TSerialConnector: public TModbusConnector {
public:
    typedef std::function<PAbstractSerialPort(const TSerialPortSettings&)> TPortMaker;
    PModbusContext CreateContext(PAbstractSerialPort port); // for tests
    PModbusContext CreateContext(const TSerialPortSettings& settings);
    static void SetGlobalPortMaker(TPortMaker maker);

private:
    static TPortMaker GlobalPortMaker;
};
