#include "fake_serial_device.h"
#include "fake_serial_port.h"
#include "ir_device_query.h"
#include "protocol_register.h"

using namespace std;

REGISTER_BASIC_INT_PROTOCOL("fake", TFakeSerialDevice, TRegisterTypes({
    { TFakeSerialDevice::REG_FAKE, "fake", "text", U16 },
}));


TFakeSerialDevice::TFakeSerialDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TFakeSerialDevice>>(config, port, protocol)
    , Connected(true)
{
    FakePort = dynamic_pointer_cast<TFakeSerialPort>(port);
    if (!FakePort) {
        throw runtime_error("not fake serial port passed to fake serial device");
    }
}

void TFakeSerialDevice::Read(const TIRDeviceQuery & query)
{
    try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        auto start = query.GetStart();
        auto end = start + query.GetCount();

        if (end > 256) {
            throw runtime_error("register address out of range");
        }

        bool blocked = any_of(query.ProtocolRegisters.begin(), query.ProtocolRegisters.end(), [this](const pair<PProtocolRegister, uint16_t> & registerIndex) {
            return Blockings[registerIndex.first->Address].first;
        });

        if (blocked) {
            throw TSerialDeviceTransientErrorException("read blocked");
        }

        if (query.GetType() != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        query.FinalizeRead(vector<uint16_t>(Registers + start, Registers + end));

        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read query '" << query.Describe() << "'";
    } catch (const exception & e) {
        FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read query '" << query.Describe() << "' failed: '" << e.what() << "'";

        throw;
    }
}

void TFakeSerialDevice::Write(const TIRDeviceValueQuery & query)
{
     try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        auto start = query.GetStart();
        auto end = start + query.GetCount();

        if (end > 256) {
            throw runtime_error("register address out of range");
        }

        bool blocked = any_of(query.ProtocolRegisters.begin(), query.ProtocolRegisters.end(), [this](const pair<PProtocolRegister, uint16_t> & registerIndex) {
            return Blockings[registerIndex.first->Address].second;
        });

        if (blocked) {
            throw TSerialDeviceTransientErrorException("write blocked");
        }

        if (query.GetType() != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        Registers[reg->Address] = value;

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
