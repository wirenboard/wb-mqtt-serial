#include "fake_serial_device.h"
#include "fake_serial_port.h"

using namespace std;

namespace // utility
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

    class TFakeRegisterRange: public TRegisterRange
    {
    public:
        TFakeRegisterRange()
        {}

        bool Add(PRegister reg, std::chrono::milliseconds pollLimit) override
        {
            if (HasOtherDeviceAndType(reg)) {
                return false;
            }
            RegisterList().push_back(reg);
            return true;
        }
    };

}; // utility

std::list<TFakeSerialDevice*> TFakeSerialDevice::Devices;

void TFakeSerialDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("fake", TRegisterTypes({{TFakeSerialDevice::REG_FAKE, "fake", "text", U16}})),
        new TBasicDeviceFactory<TFakeSerialDevice>("#/definitions/simple_device_with_setup",
                                                   "#/definitions/common_channel"));
}

TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      Connected(true),
      SessionLogEnabled(false)
{
    FakePort = dynamic_pointer_cast<TFakeSerialPort>(port);
    if (!FakePort) {
        throw runtime_error("not fake serial port passed to fake serial device");
    }
    Devices.push_back(this);
}

TRegisterValue TFakeSerialDevice::ReadRegisterImpl(const TRegisterConfig& reg)
{
    try {
        if (!FakePort->IsOpen()) {
            throw TSerialDeviceException("port not open");
        }

        if (!Connected) {
            throw TSerialDeviceTransientErrorException("device disconnected");
        }

        auto addr = GetUint32RegisterAddress(reg.GetAddress());

        if (Blockings[addr].first) {
            throw TSerialDeviceTransientErrorException("read blocked");
        }

        if (addr < 0 || addr > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg.Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        TRegisterValue value;
        if (reg.IsString()) {
            std::string str;
            for (uint32_t i = 0; i < reg.Get16BitWidth(); ++i) {
                auto ch = static_cast<char>(Registers[addr + i]);
                if (ch != '\0') {
                    str.push_back(ch);
                }
            }
            value.Set(str);
        } else {
            value.Set(GetValue(&Registers[addr], reg.Get16BitWidth()));
        }

        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg.GetAddress()
                                      << "' value '" << value << "'";
        return value;
    } catch (const exception& e) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg.GetAddress()
                                      << "' failed: '" << e.what() << "'";

        throw;
    }
}

void TFakeSerialDevice::WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& value)
{
    try {
        if (!FakePort->IsOpen()) {
            throw TSerialDeviceException("port not open");
        }

        if (!Connected) {
            throw TSerialDeviceTransientErrorException("device disconnected");
        }

        auto addr = GetUint32RegisterAddress(reg.GetAddress());

        if (Blockings[addr].second) {
            throw TSerialDeviceTransientErrorException("write blocked");
        }

        if (addr < 0 || addr > 256) {
            throw runtime_error("invalid register address");
        }

        if (reg.Type != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        if (reg.IsString()) {
            auto str = value.Get<std::string>();
            for (uint32_t i = 0; i < reg.Get16BitWidth(); ++i) {
                Registers[addr + i] = i < str.size() ? str[i] : 0;
            }
        } else {
            SetValue(&Registers[addr], reg.Get16BitWidth(), value.Get<uint64_t>());
        }
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '"
                                      << reg.GetAddress() << "' value '" << value << "'";

    } catch (const exception& e) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write address '" << reg.GetAddress()
                                      << "' failed: '" << e.what() << "'";

        throw;
    }
}

void TFakeSerialDevice::SetTransferResult(bool ok)
{
    auto initialConnectionState = GetConnectionState();
    if ((initialConnectionState != TDeviceConnectionState::CONNECTED) && ok) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': transfer OK";
    }
    if (!ok) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': transfer FAIL";
    }

    TSerialDevice::SetTransferResult(ok);

    if (initialConnectionState != GetConnectionState()) {
        if (GetConnectionState() == TDeviceConnectionState::CONNECTED) {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': reconnected";
        } else {
            FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': disconnected";
        }
    }
}

void TFakeSerialDevice::BlockReadFor(int addr, bool block)
{
    Blockings[addr].first = block;
    FakePort->GetFixture().Emit() << "fake_serial_device: " << (block ? "block" : "unblock") << " address '" << addr
                                  << "' for reading";
}

void TFakeSerialDevice::BlockWriteFor(int addr, bool block)
{
    Blockings[addr].second = block;
    FakePort->GetFixture().Emit() << "fake_serial_device: " << (block ? "block" : "unblock") << " address '" << addr
                                  << "' for writing";
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

PRegisterRange TFakeSerialDevice::CreateRegisterRange() const
{
    return std::make_shared<TFakeRegisterRange>();
}

void TFakeSerialDevice::SetSessionLogEnabled(bool enabled)
{
    SessionLogEnabled = enabled;
}

void TFakeSerialDevice::PrepareImpl()
{
    TSerialDevice::PrepareImpl();
    if (SessionLogEnabled) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': prepare";
    }
}

void TFakeSerialDevice::EndSession()
{
    if (SessionLogEnabled) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': end session";
    }
}