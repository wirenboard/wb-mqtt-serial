#pragma once

#include "serial_device.h"

#include <map>

using TMemoryBlockValueType = uint16_t;

const uint32_t FAKE_DEVICE_MEM_BLOCK_SIZE = sizeof(TMemoryBlockValueType);
const uint32_t FAKE_DEVICE_MEM_BLOCK_COUNT = 256;
const uint32_t FAKE_DEVICE_MEM_SIZE = FAKE_DEVICE_MEM_BLOCK_COUNT * FAKE_DEVICE_MEM_BLOCK_SIZE;

class TFakeSerialPort;
using PFakeSerialPort = std::shared_ptr<TFakeSerialPort>;

class TFakeSerialDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>
{
public:
    enum BlockMode: uint8_t { NONE, TRANSIENT, PERMANENT };
    enum RegisterType {REG_FAKE = 123};

    TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    void Read(const TIRDeviceQuery & query) override;
    void Write(const TIRDeviceValueQuery & query) override;
    void OnCycleEnd(bool ok) override;

    void SetMemoryBlockValue(size_t index, TMemoryBlockValueType value);
    TMemoryBlockValueType GetMemoryBlockValue(size_t index);

    void BlockReadFor(int addr, BlockMode block);
    void BlockWriteFor(int addr, BlockMode block);
    void BlockReadFor(int addr, bool block) { BlockReadFor(addr, block ? TRANSIENT: NONE); }
    void BlockWriteFor(int addr, bool block) { BlockWriteFor(addr, block ? TRANSIENT: NONE); }
    uint32_t Read2Registers(int addr);
    void SetIsConnected(bool);
    const TProtocolInfo & GetProtocolInfo() const override;

    ~TFakeSerialDevice();

    uint8_t Memory[FAKE_DEVICE_MEM_SIZE] {};
private:
    uint8_t * Block(uint32_t address);

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
