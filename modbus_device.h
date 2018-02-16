#pragma once

#include "serial_device.h"

class TModbusDevice : public TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>> {
public:
    TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    const TProtocolInfo & GetProtocolInfo() const override;
    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void ReadRegisterRange(PRegisterRange range) override;

    void Read(const PIRDeviceReadQueryEntry &) override;
    void Write(const PIRDeviceWriteQueryEntry &) override;
};
