#include "fake_serial_device.h"
#include "fake_serial_port.h"

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


TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>(config, port, protocol)
    , Connected(true)
{
    FakePort = dynamic_pointer_cast<TFakeSerialPort>(port);
    if (!FakePort) {
        throw runtime_error("not fake serial port passed to fake serial device");
    }
}

uint64_t TFakeSerialDevice::ReadRegister(PRegister reg)
{
    try {
        if (!FakePort->IsOpen()) {
            throw TSerialDeviceException("port not open");
        }

        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceTransientErrorException("device disconnected");
        }

        if(Blockings[reg->Address].first) {
            throw TSerialDeviceTransientErrorException("read blocked");
        }

        if (reg->Address < 0 || reg->Address > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg->Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        auto value = GetValue(&Registers[reg->Address], reg->Get16BitWidth());

        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' value '" << value << "'";

        return value;
    } catch (const exception & e) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' failed: '" << e.what() << "'";

        throw;
    }
}

void TFakeSerialDevice::WriteRegister(PRegister reg, uint64_t value)
{
    try {
        if (!FakePort->IsOpen()) {
            throw TSerialDeviceException("port not open");
        }

        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceTransientErrorException("device disconnected");
        }

        if(Blockings[reg->Address].second) {
            throw TSerialDeviceTransientErrorException("write blocked");
        }

        if (reg->Address < 0 || reg->Address > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg->Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        SetValue(&Registers[reg->Address], reg->Get16BitWidth(), value);

        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '" << reg->Address << "' value '" << value << "'";
    } catch (const exception & e) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write address '" << reg->Address << "' failed: '" << e.what() << "'";

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

void TFakeSerialDevice::BlockReadFor(int addr, bool block)
{
    Blockings[addr].first = block;
    FakePort->GetFixture().Emit() << "fake_serial_device: block address '" << addr << "' for reading";
}

void TFakeSerialDevice::BlockWriteFor(int addr, bool block)
{
    Blockings[addr].second = block;
    FakePort->GetFixture().Emit() << "fake_serial_device: block address '" << addr << "' for writing";
}

uint32_t TFakeSerialDevice::Read2Registers(int addr)
{
    return (uint32_t(Registers[addr]) << 16) | Registers[addr + 1];
}

void TFakeSerialDevice::SetIsConnected(bool connected)
{
    Connected = connected;
}

TFakeSerialDevice::~TFakeSerialDevice()
{}
