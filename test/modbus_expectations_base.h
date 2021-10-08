#pragma once

#include "expector.h"

#include <stdint.h>

class TModbusExpectationsBase: public virtual TExpectorProvider
{
protected:
    enum ModbusType
    {
        MODBUS_RTU
    };

    ModbusType GetSelectedModbusType() const;
    void       SelectModbusType(ModbusType selectedModbusType);

    uint8_t GetModbusRTUSlaveId() const;
    void    SetModbusRTUSlaveId(uint8_t slaveId);

    std::vector<int> WrapPDU(const std::vector<int>& pdu, uint8_t slaveId);
    std::vector<int> WrapPDU(const std::vector<int>& pdu);

private:
    ModbusType SelectedModbusType = MODBUS_RTU;
    uint8_t    RtuSlaveId         = 1;
};
