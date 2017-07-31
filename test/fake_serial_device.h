#pragma once

#include "serial_device.h"

#include <map>


class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>> {
public:
    enum RegisterType {REG_FAKE = 123};

    TFakeSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void BlockReadFor(int addr, bool block);
    void BlockWriteFor(int addr, bool block);
    ~TFakeSerialDevice();

    uint16_t Registers[256] {};
private:
    std::map<int, std::pair<bool, bool>> Blockings;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
