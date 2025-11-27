#include "modbus_expectations_base.h"

#include "crc16.h"

#include <stdexcept>

namespace
{ // utility
    std::vector<int> WrapAsRTU(const std::vector<int>& pdu, uint8_t slave_id)
    {
        std::vector<uint8_t> adu;
        adu.push_back(slave_id);
        adu.insert(adu.end(), pdu.begin(), pdu.end());
        uint16_t crc = CRC16::CalculateCRC16(adu.data(), adu.size());
        adu.push_back(crc >> 8);
        adu.push_back(crc & 0x00FF);
        return std::vector<int>(adu.begin(), adu.end());
    }
} // utility

TModbusExpectationsBase::ModbusType TModbusExpectationsBase::GetSelectedModbusType() const
{
    return SelectedModbusType;
}

void TModbusExpectationsBase::SelectModbusType(ModbusType selectedModbusType)
{
    SelectedModbusType = selectedModbusType;
}

uint8_t TModbusExpectationsBase::GetModbusRTUSlaveId() const
{
    return RtuSlaveId;
}
void TModbusExpectationsBase::SetModbusRTUSlaveId(uint8_t slaveId)
{
    RtuSlaveId = slaveId;
}

std::vector<int> TModbusExpectationsBase::WrapPDU(const std::vector<int>& pdu, uint8_t slaveId)
{
    switch (SelectedModbusType) {
        case MODBUS_RTU:
            return WrapAsRTU(pdu, slaveId);
        default:
            throw std::runtime_error("unsupported modbus type " + std::to_string(SelectedModbusType));
    }
}

std::vector<int> TModbusExpectationsBase::WrapPDU(const std::vector<int>& pdu)
{
    return WrapPDU(pdu, RtuSlaveId);
}
