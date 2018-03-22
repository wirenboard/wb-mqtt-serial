#include "fake_serial_device.h"
#include "fake_serial_port.h"
#include "ir_device_query.h"
#include "protocol_register.h"
#include "virtual_register.h"

using namespace std;

REGISTER_BASIC_INT_PROTOCOL("fake", TFakeSerialDevice, TRegisterTypes({
    { TFakeSerialDevice::REG_FAKE, "fake", "text", U16 },
}));

struct TFakeProtocolInfo: TProtocolInfo
{
    int GetMaxReadRegisters() const override
    {
        return FAKE_DEVICE_REG_COUNT;
    }

    int GetMaxWriteRegisters() const override
    {
        return FAKE_DEVICE_REG_COUNT;
    }
};


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
    const auto start = query.GetStart();
    const auto end = start + query.GetCount();

    try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        if (end > FAKE_DEVICE_REG_COUNT) {
            throw runtime_error("register address out of range");
        }

        for (auto itReg = query.RegView.Begin(); itReg != query.RegView.End(); ++itReg) {
            auto reg = *itReg;

            switch(Blockings[reg->Address].BlockRead) {
                case TRANSIENT:
                    throw TSerialDeviceTransientErrorException("read blocked (transient)");
                case PERMANENT:
                    throw TSerialDevicePermanentErrorException("read blocked (permanent)");
                default:
                    break;
            }
        }

        if (query.GetType() != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        query.FinalizeRead(vector<uint16_t>(Registers + start, Registers + end));

        if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' value '" << reg->GetValue() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read to address '" << addr << "' value '" <<  Registers[addr] << "'";
            }
        }
    } catch (const exception & e) {
        if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': read address '" << reg->Address << "' failed: '" << e.what() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetCount(); ++i) {
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
    const auto end = start + query.GetCount();

    try {
        if (!Connected || FakePort->GetDoSimulateDisconnect()) {
            throw TSerialDeviceUnknownErrorException("device disconnected");
        }

        if (end > FAKE_DEVICE_REG_COUNT) {
            throw runtime_error("register address out of range");
        }

        for (auto itReg = query.RegView.Begin(); itReg != query.RegView.End(); ++itReg) {
            auto reg = *itReg;
            switch(Blockings[reg->Address].BlockWrite) {
                case TRANSIENT:
                    throw TSerialDeviceTransientErrorException("write blocked (transient)");
                case PERMANENT:
                    throw TSerialDevicePermanentErrorException("write blocked (permanent)");
                default:
                    break;
            }
        }

        if (query.GetType() != REG_FAKE) {
            throw runtime_error("invalid register type");
        }

        query.GetValues<typename TFakeSerialDevice::RegisterValueType>(&Registers[query.GetStart()]);

        query.FinalizeWrite();

        if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '" << reg->Address << "' value '" << reg->GetValue() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetCount(); ++i) {
                auto addr = query.GetStart() + i;
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write to address '" << addr << "' value '" <<  Registers[addr] << "'";
            }
        }
    } catch (const exception & e) {
        if (!query.VirtualRegisters.empty()) {
            for (const auto & reg: query.VirtualRegisters) {
                FakePort->GetFixture().Emit() << "fake_serial_device '" << SlaveId << "': write address '" << reg->Address << "' failed: '" << e.what() << "'";
            }
        } else {
            for (uint32_t i = 0; i < query.GetCount(); ++i) {
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
    return (uint32_t(Registers[addr]) << 16) | Registers[addr + 1];
}

void TFakeSerialDevice::SetIsConnected(bool connected)
{
    Connected = connected;
}

const TProtocolInfo & TFakeSerialDevice::GetProtocolInfo() const
{
    static TFakeProtocolInfo info;
    return info;
}

TFakeSerialDevice::~TFakeSerialDevice()
{}
