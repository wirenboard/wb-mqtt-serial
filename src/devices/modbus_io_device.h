#pragma once

#include "serial_device.h"

class TModbusIODevice : public TSerialDevice, public TAggregatedSlaveId
{
public:
    TModbusIODevice(PDeviceConfig config, PPort port, PProtocol protocol);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    std::list<PRegisterRange> ReadRegisterRange(PRegisterRange range) override;
    bool WriteSetupRegisters() override;

private:
    int Shift;
};
