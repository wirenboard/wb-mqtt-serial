#pragma once

#include "expector.h"

#include <stdint.h>

class TModbusExpectations: public virtual TExpectorProvider
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

    void EnqueueCoilWriteResponse(uint8_t exception = 0);
    void EnqueueHoldingWriteU16Response(uint8_t exception = 0);

    void EnqueueCoilWriteMultipleResponse(uint8_t exception = 0);
    void EnqueueHoldingWriteS64Response(uint8_t exception = 0);

    void EnqueueRGBWriteResponse(uint8_t exception = 0);

protected:
    enum ModbusType {MODBUS_RTU};

    ModbusType GetSelectedModbusType() const;
    void SelectModbusType(ModbusType selectedModbusType);

private:
    ModbusType SelectedModbusType = MODBUS_RTU;

    std::vector<int> WrapPDU(const std::vector<int>& pdu);
};


