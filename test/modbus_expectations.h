#pragma once

#include "modbus_expectations_base.h"

class TModbusExpectations: public TModbusExpectationsBase
{
public:
    void EnqueueCoilReadResponse(uint8_t exception = 0);

    void EnqueueHoldingPackReadResponse(uint8_t exception = 0);
    void EnqueueHoldingPackHoles10ReadResponse(uint8_t exception = 0);
    void EnqueueHoldingPackMax3ReadResponse(uint8_t exception = 0);

    void EnqueueDiscreteReadResponse(uint8_t exception = 0);
    void EnqueueHoldingReadS64Response(uint8_t exception = 0);
    void EnqueueHoldingReadF32Response(uint8_t exception = 0);
    void EnqueueHoldingReadU16Response(uint8_t exception = 0);
    void EnqueueInputReadU16Response(uint8_t exception = 0);

    void EnqueueHoldingSingleMax3ReadResponse(uint8_t exception = 0);
    void EnqueueHoldingMultiMax3ReadResponse(uint8_t exception = 0);

    void EnqueueHoldingSingleReadResponse(uint8_t exception = 0);
    void EnqueueHoldingMultiReadResponse(uint8_t exception = 0);

    void EnqueueHoldingSingleWriteU64Response(uint8_t exception = 0);
    void EnqueueHoldingSingleWriteU16Response(uint8_t exception = 0);
    void EnqueueHoldingMultiWriteU64Response(uint8_t exception = 0);
    void EnqueueHoldingMultiWriteU16Response(uint8_t exception = 0);

    void EnqueueCoilWriteResponse(uint8_t exception = 0);
    void EnqueueHoldingWriteU16Response(uint8_t exception = 0);

    void EnqueueCoilWriteMultipleResponse(uint8_t exception = 0);
    void EnqueueHoldingWriteS64Response(uint8_t exception = 0);

    void EnqueueRGBWriteResponse(uint8_t exception = 0);

    void Enqueue10CoilsReadResponse(uint8_t exception = 0);

    void Enqueue10CoilsMax3ReadResponse(uint8_t exception = 0);

    void EnqueueInvalidCRCCoilReadResponse();
    void EnqueueWrongDataSizeReadResponse();
    void EnqueueWrongSlaveIdCoilReadResponse(uint8_t exception = 0);
    void EnqueueWrongFunctionCodeCoilReadResponse(uint8_t exception = 0);
    void EnqueueWrongSlaveIdCoilWriteResponse(uint8_t exception = 0);
    void EnqueueWrongFunctionCodeCoilWriteResponse(uint8_t exception = 0);

    /*------------ bitmasks ----------------*/

    void EnqueueU8Shift0Bits8HoldingReadResponse();
    void EnqueueU16Shift8HoldingReadResponse(bool afterWrite);
    void EnqueueU8Shift0SingleBitHoldingReadResponse(bool afterWrite);
    void EnqueueU8Shift1SingleBitHoldingReadResponse(bool afterWrite);
    void EnqueueU8Shift2SingleBitHoldingReadResponse(bool afterWrite);

    void EnqueueU8Shift1SingleBitHoldingWriteResponse();
    void EnqueueU16Shift8HoldingWriteResponse();
};
