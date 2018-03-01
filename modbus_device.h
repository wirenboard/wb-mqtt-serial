#pragma once

#include "serial_device.h"

class TModbusDevice : public TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>> {
public:
    TModbusDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    const TProtocolInfo & GetProtocolInfo() const override;

    void Read(const TIRDeviceQuery &) override;
    void Write(const TIRDeviceValueQuery &) override;
};
