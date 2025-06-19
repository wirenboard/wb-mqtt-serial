#pragma once

#include "modbus_expectations_base.h"

class TModbusExpectations: public TModbusExpectationsBase
{
public:
    enum StringReadHint
    {
        FULL_OF_CHARS,
        TRAILING_ZEROS,
        ZERO_AND_TRASH,
        TRAILING_FF
    };

    void EnqueueCoilReadResponse(uint8_t exception = 0);
    void EnqueueCoilOneByOneReadResponse(uint8_t exception = 0);

    void EnqueueHoldingPackReadResponse(uint8_t exception = 0);
    void EnqueueHoldingPackHoles10ReadResponse(uint8_t exception = 0);
    void EnqueueHoldingPackMax3ReadResponse(uint8_t exception = 0);
    void EnqueueHoldingPartialPackReadResponse(uint8_t exception = 0);

    void EnqueueHoldingPackUnavailableOnBorderReadResponse();
    void EnqueueHoldingPackUnavailableInTheMiddleReadResponse();
    void EnqueueHoldingPackUnavailableAndHolesReadResponse();

    void EnqueueHoldingUnsupportedOnBorderReadResponse();

    void EnqueueDiscreteReadResponse(uint8_t exception = 0);
    void EnqueueHoldingReadS64Response(uint8_t exception = 0);
    void EnqueueStringReadResponse(StringReadHint hint = TRAILING_ZEROS);
    void EnqueueStringWriteResponse();
    void EnqueueString8ReadResponse(StringReadHint hint = TRAILING_ZEROS);
    void EnqueueString8WriteResponse();
    void EnqueueHoldingReadF32Response(uint8_t exception = 0);
    void EnqueueHoldingReadU16Response(uint8_t exception = 0, bool timeout = false);
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

    void EnqueueHoldingWriteU16ResponseWithWriteAddress(uint8_t exception = 0);
    void EnqueueHoldingReadU16ResponseWithWriteAddress(uint8_t exception = 0);
    void EnqueueHoldingReadU16Min2ReadResponse(uint8_t exception = 0);

    void EnqueueHoldingWriteU16ResponseWithOffsetWriteOptions(uint8_t exception = 0);
    void EnqueueHoldingReadU16ResponseWithOffsetWriteOptions(uint8_t exception = 0);

    void EnqueueCoilWriteMultipleResponse(uint8_t exception = 0);
    void EnqueueHoldingWriteS64Response(uint8_t exception = 0);

    void EnqueueRGBWriteResponse(uint8_t exception = 0);

    void Enqueue10CoilsReadResponse(uint8_t exception = 0);
    void EnqueueCoilsPackReadResponse(uint8_t exception = 0);

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

    void EnqueueU32BitsHoldingReadResponse(bool afterWrite);
    void EnqueueU32BitsHoldingWriteResponse();

    void EnqueueU8Shift1SingleBitHoldingWriteResponse();
    void EnqueueU16Shift8HoldingWriteResponse();

    void EnqueueHoldingSeparateReadResponse(uint8_t exception = 0);
    void EnqueueHoldingSingleOneByOneReadResponse(uint8_t exception = 0);
    void EnqueueHoldingMultiOneByOneReadResponse(uint8_t exception = 0);

    /*--------------------------------------*/

    void EnqueueContinuousReadEnableResponse(bool ok = true);
    void EnqueueContinuousReadResponse(bool separated = true);

    void EnqueueContinuousWriteResponse();
    void EnqueueContinuousWriteReadChannelResponse();

    void EnqueueReadResponseWithNoiseAtTheEnd(bool addNoise);

    void EnqueueLittleEndianReadResponses();
    void EnqueueLittleEndianWriteResponses();

    /*--------------------------------------*/
    void EnqueueInputReadResponse(uint8_t addrLow, const std::vector<int>& data);
};
