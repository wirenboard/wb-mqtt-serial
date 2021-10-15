#pragma once

#include "modbus_expectations_base.h"

class TModbusIOExpectations: public TModbusExpectationsBase
{
public:
    void EnqueueSetupSectionWriteResponse(bool firstModule, bool error = false);
    void EnqueueCoilReadResponse(bool firstModule);
    void EnqueueCoilWriteResponse(bool firstModule);
};
