#pragma once

#include "serial_device.h"

#include <map>

class TFakeSerialPort;
using PFakeSerialPort = std::shared_ptr<TFakeSerialPort>;

class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>
{
public:
    enum BlockMode: uint8_t { NONE, TRANSIENT, PERMANENT };
    enum RegisterType {REG_FAKE = 123};

    TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    uint64_t ReadRegister(PRegister reg) override;
    void WriteRegister(PRegister reg, uint64_t value) override;
    void OnCycleEnd(bool ok) override;
    void BlockReadFor(int addr, BlockMode block);
    void BlockWriteFor(int addr, BlockMode block);
    void BlockReadFor(int addr, bool block) { BlockReadFor(addr, block ? TRANSIENT: NONE); }
    void BlockWriteFor(int addr, bool block) { BlockWriteFor(addr, block ? TRANSIENT: NONE); }
    uint32_t Read2Registers(int addr);
    void SetIsConnected(bool);
    ~TFakeSerialDevice();

    uint16_t Registers[256] {};
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
