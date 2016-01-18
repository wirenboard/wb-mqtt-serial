#include "testlog.h"
#include "fake_serial_port.h"
#include "serial_connector.h"

#include "uniel_expectations.h"

class TUnielProtocolTest: public TSerialProtocolDirectTest, public TUnielProtocolExpectations {
protected:
    void SetUp();
    void TearDown();
};

void TUnielProtocolTest::SetUp()
{
    TSerialProtocolDirectTest::SetUp();
    Context->AddDevice(std::make_shared<TDeviceConfig>("uniel", 0x01, "uniel"));
}

void TUnielProtocolTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialProtocolDirectTest::TearDown();
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

class TUnielIntegrationTest: public TSerialProtocolIntegrationTest, public TUnielProtocolExpectations {
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-uniel-test.json"; }
};

void TUnielIntegrationTest::SetUp()
{
    TSerialProtocolIntegrationTest::SetUp();
}

void TUnielIntegrationTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialProtocolIntegrationTest::TearDown();
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
