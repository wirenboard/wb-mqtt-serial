#pragma once

#include "serial_device.h"

#include <map>


class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>> {
public:
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    TFakeSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void BlockReadFor(RegisterType type, int addr, bool block);
    void BlockWriteFor(RegisterType type, int addr, bool block);
    ~TFakeSerialDevice();

    uint8_t Coils[256] {}, Discrete[256] {};
    uint16_t Holding[256] {}, Input[256] {};
private:
    std::map<int, std::map<int, std::pair<bool, bool>>> Blockings;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
