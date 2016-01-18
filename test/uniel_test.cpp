#include "testlog.h"
#include "fake_serial_port.h"
#include "serial_connector.h"

class TUnielProtocolTestBase: public virtual TSerialProtocolTest
{
protected:
    void TearDown();
    void EnqueueVoltageQueryResponse();
    void EnqueueRelayOffQueryResponse();
    void EnqueueRelayOnQueryResponse();
    void EnqueueThreshold0QueryResponse();
    void EnqueueBrightnessQueryResponse();
    void EnqueueSetRelayOnResponse();
    void EnqueueSetRelayOffResponse();
    void EnqueueSetLowThreshold0Response();
    void EnqueueSetBrightnessResponse();
};

void TUnielProtocolTestBase::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialProtocolTest::TearDown();
}

void TUnielProtocolTestBase::EnqueueVoltageQueryResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x0a, // addr
            0x00, // not used
            0x10  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x9a, // value = 9a (154)
            0x0a, // addr
            0x00, // not used
            0xa9  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueRelayOffQueryResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x00, // value = 00 (off)
            0x1b, // addr
            0x00, // not used
            0x20  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueRelayOnQueryResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0xff, // value = 0xff (on)
            0x1b, // addr
            0x00, // not used
            0x1f  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueThreshold0QueryResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x02, // addr
            0x00, // not used
            0x08  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x70, // value = 0xff (on)
            0x02, // addr
            0x00, // not used
            0x77  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueBrightnessQueryResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x01, // unit id
            0x00, // not used
            0x41, // addr
            0x00, // not used
            0x47  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x05, // op (5 = read)
            0x00, // unit id 0 = module response
            0x42, // value = 66
            0x41, // addr
            0x00, // not used
            0x88  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueSetRelayOnResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0xff, // relay on
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0xff, // relay on
            0x1b, // addr
            0x00, // not used
            0x20  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueSetRelayOffResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0x00, // relay off
            0x1b, // addr
            0x00, // not used
            0x22  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0x00, // relay on
            0x1b, // addr
            0x00, // not used
            0x21  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueSetLowThreshold0Response()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x01, // unit id
            0x70, // value = 112
            0x02, // addr
            0x00, // not used
            0x79  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x06, // op (6 = write)
            0x00, // unit id 0 = module response
            0x70, // value = 112
            0x02, // addr
            0x00, // not used
            0x78  // sum
        }, __func__);
}

void TUnielProtocolTestBase::EnqueueSetBrightnessResponse()
{
    SerialPort->Expect(
        {
            0xff, // sync
            0xff, // sync
            0x0a, // op (0x0a = set brightness)
            0x01, // unit id
            0x42, // value = 66
            0x01, // addr
            0x00, // not used
            0x4e  // sum
        },
        {
            0xff, // sync
            0xff, // sync
            0x0a, // op (0x0a = set brightness)
            0x00, // unit id 0 = module response
            0x42, // value = 66
            0x01, // addr
            0x00, // not used
            0x4d  // sum
        }, __func__);
}

class TUnielProtocolTest: public TSerialProtocolDirectTest, public TUnielProtocolTestBase {
protected:
    void SetUp();
    void TearDown();
};

void TUnielProtocolTest::SetUp()
{
    TUnielProtocolTestBase::SetUp();
    TSerialProtocolDirectTest::SetUp();
    Context->AddDevice(std::make_shared<TDeviceConfig>("uniel", 0x01, "uniel"));
}

void TUnielProtocolTest::TearDown()
{
    TSerialProtocolDirectTest::TearDown();
    TUnielProtocolTestBase::TearDown();
}

TEST_F(TUnielProtocolTest, TestQuery)
{
    Context->SetSlave(0x01);

    EnqueueVoltageQueryResponse();
    uint16_t v;
    Context->ReadHoldingRegisters(0x0a, 1, &v);
    ASSERT_EQ(154, v);

    // input: same as holding
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    Context->ReadInputRegisters(0x0a, 1, &v);
    ASSERT_EQ(154, v);

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    uint8_t b;
    Context->ReadCoils(0x1b, 1, &b);
    ASSERT_EQ(0, b);

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    Context->ReadCoils(0x1b, 1, &b);
    ASSERT_EQ(1, b);

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    Context->ReadHoldingRegisters(0x02, 1, &v);
    ASSERT_EQ(0x70, v);

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    Context->ReadHoldingRegisters(0x01000141, 1, &v);
    ASSERT_EQ(66, v);
}

TEST_F(TUnielProtocolTest, TestSetRelayState)
{
    Context->SetSlave(0x01);
    EnqueueSetRelayOnResponse();
    Context->WriteCoil(0x1b, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Context->WriteCoil(0x1b, 0);
}

TEST_F(TUnielProtocolTest, TestSetParam)
{
    Context->SetSlave(0x01);
    EnqueueSetLowThreshold0Response();
    Context->WriteHoldingRegister(0x02, 0x70);
}

TEST_F(TUnielProtocolTest, TestSetBrightness)
{
    Context->SetSlave(0x01);
    EnqueueSetBrightnessResponse();
    Context->WriteHoldingRegister(0x01000141, 0x42);
}

class TUnielIntegrationTest: public TSerialProtocolIntegrationTest, public TUnielProtocolTestBase {
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-uniel-test.json"; }
};

void TUnielIntegrationTest::SetUp()
{
    TUnielProtocolTestBase::SetUp();
    TSerialProtocolIntegrationTest::SetUp();
}

void TUnielIntegrationTest::TearDown()
{
    TSerialProtocolIntegrationTest::TearDown();
    TUnielProtocolTestBase::TearDown();
}

TEST_F(TUnielIntegrationTest, Poll)
{
    Observer->SetUp();
    ASSERT_TRUE(!!SerialPort);

    EnqueueVoltageQueryResponse();
    EnqueueRelayOffQueryResponse();
    EnqueueThreshold0QueryResponse();
    EnqueueBrightnessQueryResponse();

    Note() << "ModbusLoopOnce()";
    Observer->ModbusLoopOnce();
    SerialPort->DumpWhatWasRead();

    MQTTClient->DoPublish(true, 0, "/devices/pseudo_uniel/controls/Relay 1/on", "1");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_uniel/controls/LowThr/on", "112");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_uniel/controls/LED 1/on", "66");

    EnqueueSetRelayOnResponse();
    EnqueueSetLowThreshold0Response();
    EnqueueSetBrightnessResponse();

    EnqueueVoltageQueryResponse();
    EnqueueRelayOnQueryResponse();
    EnqueueThreshold0QueryResponse();
    EnqueueBrightnessQueryResponse();

    Note() << "ModbusLoopOnce()";
    Observer->ModbusLoopOnce();
    SerialPort->DumpWhatWasRead();
}
