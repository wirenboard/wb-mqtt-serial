#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

#include "modbus_common.h"

class TModbusDevice : public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;

public:
    TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits, PDeviceConfig config, PPort port, PProtocol protocol);
    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, bool enableHoles = true) const override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    std::list<PRegisterRange> ReadRegisterRange(PRegisterRange range) override;
    bool WriteSetupRegisters() override;

    static void Register(TSerialDeviceFactory& factory);
};
