#pragma once

#include "serial_device.h"

#include <map>

const uint32_t FAKE_DEVICE_REG_COUNT = 256;

class TFakeSerialPort;
using PFakeSerialPort = std::shared_ptr<TFakeSerialPort>;

class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>
{
public:
    using RegisterValueType = uint16_t;

    enum BlockMode: uint8_t { NONE, TRANSIENT, PERMANENT };
    enum RegisterType {REG_FAKE = 123};

    TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    void Read(const TIRDeviceQuery & query) override;
    void Write(const TIRDeviceValueQuery & query) override;
    void OnCycleEnd(bool ok) override;
    void BlockReadFor(int addr, BlockMode block);
    void BlockWriteFor(int addr, BlockMode block);
    void BlockReadFor(int addr, bool block) { BlockReadFor(addr, block ? TRANSIENT: NONE); }
    void BlockWriteFor(int addr, bool block) { BlockWriteFor(addr, block ? TRANSIENT: NONE); }
    uint32_t Read2Registers(int addr);
    void SetIsConnected(bool);
    const TProtocolInfo & GetProtocolInfo() const override;
    ~TFakeSerialDevice();

    RegisterValueType Registers[FAKE_DEVICE_REG_COUNT] {};
private:
    struct TBlockInfo
    {
        BlockMode BlockRead,
                  BlockWrite;
    };

    std::map<int, TBlockInfo> Blockings;
    PFakeSerialPort           FakePort;
    bool                      Connected;
};

typedef std::shared_ptr<TFakeSerialDevice> PFakeSerialDevice;
