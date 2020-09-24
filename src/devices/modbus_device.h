#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TModbusDevice : public TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>> {
public:
    TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void ReadRegisterRange(PRegisterRange range) override;
};
