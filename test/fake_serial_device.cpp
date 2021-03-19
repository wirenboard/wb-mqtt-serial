#include "fake_serial_device.h"
#include "fake_serial_port.h"

using namespace std;

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

std::list<TFakeSerialDevice*> TFakeSerialDevice::Devices;

void TFakeSerialDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("fake", TRegisterTypes({{ TFakeSerialDevice::REG_FAKE, "fake", "text", U16 }})),
                             new TBasicDeviceFactory<TFakeSerialDevice>());
}

TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol), TUInt32SlaveId(config->SlaveId)
    , Connected(true)
{
    FakePort = dynamic_pointer_cast<TFakeSerialPort>(port);
    if (!FakePort) {
        throw runtime_error("not fake serial port passed to fake serial device");
    }
    Devices.push_back(this);
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

        auto addr = GetUint32RegisterAddress(*reg->Address);

        if(Blockings[addr].first) {
            throw TSerialDeviceTransientErrorException("read blocked");
        }

        if (addr < 0 || addr > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg->Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        auto value = GetValue(&Registers[addr], reg->Get16BitWidth());

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

        auto addr = GetUint32RegisterAddress(*reg->Address);

        if(Blockings[addr].second) {
            throw TSerialDeviceTransientErrorException("write blocked");
        }

        if (addr < 0 || addr > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg->Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        SetValue(&Registers[addr], reg->Get16BitWidth(), value);

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
{
    Devices.erase(std::remove(Devices.begin(), Devices.end(), this), Devices.end());
}

TFakeSerialDevice* TFakeSerialDevice::GetDevice(const std::string& slaveId)
{
    for (auto device: Devices) {
        if (device->DeviceConfig()->SlaveId == slaveId) {
            return device;
        }
    }
    return nullptr;
}

void TFakeSerialDevice::ClearDevices()
{
    Devices.clear();
}