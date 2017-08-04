#pragma once

#include "modbus_expectations_base.h"

class TModbusIOExpectations: public TModbusExpectationsBase
{
public:
    void EnqueueSetupSectionWriteResponse();
    void EnqueueCoilReadResponse();
    void EnqueueCoilWriteResponse();
};


