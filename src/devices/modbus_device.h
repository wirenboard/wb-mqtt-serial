#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TModbusDevice : public TSerialDevice, public TUInt32SlaveId
{
public:
    static const int DefaultTimeoutMs = 1000;

    TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void ReadRegisterRange(PRegisterRange range) override;
};


class TModbusTCPDevice : public TSerialDevice, public TUInt32SlaveId
{
    std::shared_ptr<uint16_t> TransactionId;
public:
    TModbusTCPDevice(PDeviceConfig config, PPort port, PProtocol protocol, std::shared_ptr<uint16_t> transactionId);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void ReadRegisterRange(PRegisterRange range) override;
};