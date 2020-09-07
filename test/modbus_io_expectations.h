#pragma once

#include "modbus_expectations_base.h"

class TModbusIOExpectations: public TModbusExpectationsBase
{
public:
    void EnqueueSetupSectionWriteResponse(bool firstModule);
    void EnqueueCoilReadResponse(bool firstModule);
    void EnqueueCoilWriteResponse();
};


