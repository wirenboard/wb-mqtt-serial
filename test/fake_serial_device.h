#pragma once

#include "serial_device.h"

#include <map>
#include <functional>


class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>> {
public:
    enum RegisterType {REG_FAKE = 123};
    using TReadCallback = std::function<void (PRegister)>;
    using TWriteCallback = std::function<void (PRegister, uint64_t)>;

    TFakeSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void BlockReadFor(int addr, bool block);
    void BlockWriteFor(int addr, bool block);
    uint32_t Read2Registers(int addr);
    void SetReadCallback(TReadCallback);
    void SetWriteCallback(TWriteCallback);
    ~TFakeSerialDevice();

    uint16_t Registers[256] {};
private:
    std::map<int, std::pair<bool, bool>> Blockings;
    TReadCallback ReadCallback;
    TWriteCallback WriteCallback;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
