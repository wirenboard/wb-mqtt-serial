#pragma once

#include "serial_device.h"

#include <map>

class TFakeSerialPort;
using PFakeSerialPort = std::shared_ptr<TFakeSerialPort>;

class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>> {
public:
    using RegisterValueType = uint16_t;

    enum RegisterType {REG_FAKE = 123};

    TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    void Read(const TIRDeviceQuery & query) override;
    void Write(const TIRDeviceValueQuery & query) override;
    void OnCycleEnd(bool ok) override;

    void BlockReadFor(int addr, bool block);
    void BlockWriteFor(int addr, bool block);
    uint32_t Read2Registers(int addr);
    void SetIsConnected(bool);
    ~TFakeSerialDevice();

    RegisterValueType Registers[256] {};
private:
    PFakeSerialPort FakePort;
    std::map<int, std::pair<bool, bool>> Blockings;
    bool Connected;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
