#include "fake_serial_device.h"

using namespace std;

REGISTER_BASIC_INT_PROTOCOL("fake", TFakeSerialDevice, TRegisterTypes({
    { TFakeSerialDevice::REG_COIL, "coil", "switch", U8 },
    { TFakeSerialDevice::REG_DISCRETE, "discrete", "switch", U8, true },
    { TFakeSerialDevice::REG_HOLDING, "holding", "text", U16 },
    { TFakeSerialDevice::REG_INPUT, "input", "text", U16, true }
}));


namespace { //utility
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


TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol):
    TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>(config, port, protocol)
{}

uint64_t TFakeSerialDevice::ReadRegister(PRegister reg)
{
    if(Blockings[reg->Type][reg->Address].first) {
        throw TSerialDeviceTransientErrorException("read blocked");
    }

    switch (reg->Type) {
    case REG_COIL:      return Coils[reg->Address];
    case REG_DISCRETE:  return Discrete[reg->Address];
    case REG_HOLDING:   return GetValue(&Holding[reg->Address], reg->Width());
    case REG_INPUT:     return GetValue(&Input[reg->Address], reg->Width());
    default: throw runtime_error("invalid register type");
    }
}

void TFakeSerialDevice::WriteRegister(PRegister reg, uint64_t value)
{
    if(Blockings[reg->Type][reg->Address].second) {
        throw TSerialDeviceTransientErrorException("write blocked");
    }

    switch (reg->Type) {
    case REG_COIL:
        Coils[reg->Address] = value ? 1: 0;
        break;
    case REG_DISCRETE:
        Discrete[reg->Address] = value ? 1: 0;
        break;
    case REG_HOLDING:
        SetValue(&Holding[reg->Address], reg->Width(), value);
        break;
    case REG_INPUT:
        SetValue(&Input[reg->Address], reg->Width(), value);
        break;
    default:
        throw runtime_error("invalid register type");
    }
}

void TFakeSerialDevice::BlockReadFor(RegisterType type, int addr, bool block)
{
    Blockings[type][addr].first = block;
}

void TFakeSerialDevice::BlockWriteFor(RegisterType type, int addr, bool block)
{
    Blockings[type][addr].second = block;
}

TFakeSerialDevice::~TFakeSerialDevice()
{}

