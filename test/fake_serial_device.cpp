#include "fake_serial_device.h"
#include "fake_serial_port.h"
#include "ir_device_query.h"
#include "memory_block.h"

using namespace std;

REGISTER_BASIC_INT_PROTOCOL("fake", TFakeSerialDevice, TRegisterTypes({
    { TFakeSerialDevice::REG_FAKE, "fake", "text", { U16 } },
}));

struct TFakeProtocolInfo: TProtocolInfo
{
    uint32_t GetMaxReadRegisters() const override
    {
        return FAKE_DEVICE_MEM_BLOCK_COUNT;
    }

    uint32_t GetMaxWriteRegisters() const override
    {
        return FAKE_DEVICE_MEM_BLOCK_COUNT;
    }
};


TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>(config, port, protocol)
    , Connected(true)
    , LogQueries(false)
{
    FakePort = dynamic_pointer_cast<TFakeSerialPort>(port);
    if (!FakePort) {
        throw runtime_error("not fake serial port passed to fake serial device");
    }
}

uint8_t * TFakeSerialDevice::Block(uint32_t address)
{
    return Memory + address * FAKE_DEVICE_MEM_BLOCK_SIZE;
}

void TFakeSerialDevice::Read(const TIRDeviceQuery & query)
{
    const auto start = query.GetStart();
    const auto end = start + query.GetBlockCount();

    try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        if (end > FAKE_DEVICE_MEM_BLOCK_COUNT) {
            throw runtime_error("register address out of range");
        }

        for (const auto & mb: query.MemoryBlockRange) {
            switch(Blockings[mb->Address].BlockRead) {
                case TRANSIENT:
                    throw TSerialDeviceTransientErrorException("read blocked (transient)");
                case PERMANENT:
                    throw TSerialDevicePermanentErrorException("read blocked (permanent)");
                default:
                    break;
            }
        }

        if (query.GetType().Index != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        query.FinalizeRead(Block(start), query.GetSize());

        if (LogQueries) {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': " << query.Describe();
        } else if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' value '" << reg->GetTextValue() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetBlockCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read to address '" << addr << "' value '" <<  GetMemoryBlockValue(addr) << "'";
            }
        }
    } catch (const exception & e) {
        if (LogQueries) {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': " << query.Describe() << " failed: '" << e.what() << "'";
        } else if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' failed: '" << e.what() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetBlockCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << addr << "' failed: '" << e.what() << "'";
            }
        }

        throw;
    }
}

void TFakeSerialDevice::Write(const TIRDeviceValueQuery & query)
{
    const auto start = query.GetStart();
    const auto end = start + query.GetBlockCount();

    try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        if (end > FAKE_DEVICE_MEM_BLOCK_COUNT) {
            throw runtime_error("register address out of range");
        }

        for (const auto & mb: query.MemoryBlockRange) {
            switch(Blockings[mb->Address].BlockWrite) {
                case TRANSIENT:
                    throw TSerialDeviceTransientErrorException("write blocked (transient)");
                case PERMANENT:
                    throw TSerialDevicePermanentErrorException("write blocked (permanent)");
                default:
                    break;
            }
        }

        if (query.GetType().Index != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        query.GetValues(Block(query.GetStart()), query.GetSize());

        query.FinalizeWrite();

        if (LogQueries) {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': " << query.Describe();
        } else if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '" << reg->Address << "' value '" << reg->GetTextValue() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetBlockCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '" << addr << "' value '" <<  GetMemoryBlockValue(addr) << "'";
            }
        }
    } catch (const exception & e) {
        if (LogQueries) {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': " << query.Describe() << " failed: '" << e.what() << "'";
        } else if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write address '" << reg->Address << "' failed: '" << e.what() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetBlockCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write address '" << addr << "' failed: '" << e.what() << "'";
            }
        }

        throw;
    }
}

void TFakeSerialDevice::OnCycleEnd(bool ok)
{
    FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': " << (ok ? "Device cycle OK" : "Device cycle FAIL");

    bool wasDisconnected = GetIsDisconnected();

    TSerialDevice::OnCycleEnd(ok);

    if (wasDisconnected && !GetIsDisconnected()) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': reconnected";
    } else if (!wasDisconnected && GetIsDisconnected()) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': disconnected";
    }
}

/* set big endian block value */
void TFakeSerialDevice::SetMemoryBlockValue(size_t index, TMemoryBlockValueType value)
{
    int startByte = index * FAKE_DEVICE_MEM_BLOCK_SIZE;

    for(int iByte = startByte + FAKE_DEVICE_MEM_BLOCK_SIZE - 1; iByte >= startByte; --iByte) {
        Memory[iByte] = value;
        value >>= 8;
    }
}

/* read big endian block value */
TMemoryBlockValueType TFakeSerialDevice::GetMemoryBlockValue(size_t index)
{
    TMemoryBlockValueType value = 0;

    auto startByte = index * FAKE_DEVICE_MEM_BLOCK_SIZE;

    for(auto iByte = startByte; iByte < startByte + FAKE_DEVICE_MEM_BLOCK_SIZE; ++iByte) {
        value <<= 8;
        value |= Memory[iByte];
    }

    return value;
}

void TFakeSerialDevice::BlockReadFor(int addr, BlockMode block)
{
    Blockings[addr].BlockRead = block;

    FakePort->GetFixture().Emit() << "fake_serial_device: block address '" << addr << "' for reading";
}

void TFakeSerialDevice::BlockWriteFor(int addr, BlockMode block)
{
    Blockings[addr].BlockWrite = block;

    FakePort->GetFixture().Emit() << "fake_serial_device: block address '" << addr << "' for writing";
}

uint32_t TFakeSerialDevice::Read2Registers(int addr)
{
    return (uint32_t(GetMemoryBlockValue(addr)) << 16) | GetMemoryBlockValue(addr + 1);
}

void TFakeSerialDevice::SetIsConnected(bool connected)
{
    Connected = connected;
}

void TFakeSerialDevice::SetLogQueries(bool logQueries)
{
    LogQueries = logQueries;
}

const TProtocolInfo & TFakeSerialDevice::GetProtocolInfo() const
{
    static TFakeProtocolInfo info;
    return info;
}

TFakeSerialDevice::~TFakeSerialDevice()
{}
