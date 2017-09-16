#include "fake_serial_device.h"

using namespace std;

REGISTER_BASIC_INT_PROTOCOL("fake", TFakeSerialDevice, TRegisterTypes({
    { TFakeSerialDevice::REG_FAKE, "fake", "text", U16 },
}));


namespace //utility
{
    uint64_t GetValue(uint16_t* src, int width)
    {
        uint64_t res = 0;
        for (int i = 0; i < width; ++i) {
            auto bitCount = ((width - i) - 1) * 16;
            res |= uint64_t(src[i]) << bitCount;
        }
        return res;
    }

    void SetValue(uint16_t* dst, int width, uint64_t value)
    {
        for (int p = width - 1; p >= 0; --p) {
            dst[p] = value & 0xffff;
            value >>= 16;
        }
    }
};  //utility


TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>(config, port, protocol)
    , ReadCallback([](PRegister){})
    , WriteCallback([](PRegister, uint64_t){})
{}

uint64_t TFakeSerialDevice::ReadRegister(PRegister reg)
{
    ReadCallback(reg);
    if(Blockings[reg->Address].first) {
        throw TSerialDeviceTransientErrorException("read blocked for register " + to_string(reg->Address));
    }

    if (reg->Address < 0 || reg->Address > 256) {
        throw runtime_error("invalid register address");
    }
    
    if (reg->Type != REG_FAKE) {
        throw runtime_error("invalid register type");
    }

    return GetValue(&Registers[reg->Address], reg->Width());
}

void TFakeSerialDevice::WriteRegister(PRegister reg, uint64_t value)
{
    WriteCallback(reg, value);
    if(Blockings[reg->Address].second) {
        throw TSerialDeviceTransientErrorException("write blocked for register " + to_string(reg->Address));
    }

    if (reg->Address < 0 || reg->Address > 256) {
        throw runtime_error("invalid register address");
    }
    
    if (reg->Type != REG_FAKE) {
        throw runtime_error("invalid register type");
    }

    SetValue(&Registers[reg->Address], reg->Width(), value);
}

void TFakeSerialDevice::BlockReadFor(int addr, bool block)
{
    Blockings[addr].first = block;
}

void TFakeSerialDevice::BlockWriteFor(int addr, bool block)
{
    Blockings[addr].second = block;
}

uint32_t TFakeSerialDevice::Read2Registers(int addr)
{
    return (uint32_t(Registers[addr]) << 16) | Registers[addr + 1];
}

void TFakeSerialDevice::SetReadCallback(TReadCallback read_callback)
{
    ReadCallback = read_callback;
}

void TFakeSerialDevice::SetWriteCallback(TWriteCallback write_callback)
{
    WriteCallback = write_callback;
}

TFakeSerialDevice::~TFakeSerialDevice()
{}

