#pragma once

#include "serial_device.h"

class TModbusIODevice : public TBasicProtocolSerialDevice<TBasicProtocol<TModbusIODevice, TAggregatedSlaveId>> {
public:
    TModbusIODevice(PDeviceConfig config, PPort port, PProtocol protocol);

    const TProtocolInfo & GetProtocolInfo() const override;
    void Read(const TIRDeviceQuery &) override;
    void Write(const TIRDeviceValueQuery &) override;

private:
    int Shift;
};
