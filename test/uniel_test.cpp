#include "testlog.h"
#include "fake_serial_port.h"
#include "uniel_protocol.h"
#include "uniel_expectations.h"

namespace {
    PRegister InputReg(new TRegister(1, TUnielProtocol::REG_INPUT, 0x0a, U8));
    PRegister RelayReg(new TRegister(1, TUnielProtocol::REG_RELAY, 0x1b, U8));
    PRegister ThresholdReg(new TRegister(1, TUnielProtocol::REG_PARAM, 0x02, U8));
    PRegister BrightnessReg(new TRegister(1, TUnielProtocol::REG_BRIGHTNESS, 0x141, U8));
};

class TUnielProtocolTest: public TSerialProtocolTest, public TUnielProtocolExpectations {
protected:
    void SetUp();
    void TearDown();
    PUnielProtocol Proto;
};

void TUnielProtocolTest::SetUp()
{
    TSerialProtocolTest::SetUp();
    Proto = std::make_shared<TUnielProtocol>(
        std::make_shared<TDeviceConfig>("uniel", 0x01, "uniel"),
        SerialPort);
    SerialPort->Open();
}

void TUnielProtocolTest::TearDown()
{
    Proto.reset();
    SerialPort->DumpWhatWasRead();
    TSerialProtocolTest::TearDown();
}

TEST_F(TUnielProtocolTest, TestQuery)
{
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, Proto->ReadRegister(InputReg));

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, Proto->ReadRegister(InputReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    ASSERT_EQ(0, Proto->ReadRegister(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    ASSERT_EQ(1, Proto->ReadRegister(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    ASSERT_EQ(0x70, Proto->ReadRegister(ThresholdReg));

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    ASSERT_EQ(66, Proto->ReadRegister(BrightnessReg));
}

TEST_F(TUnielProtocolTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    Proto->WriteRegister(RelayReg, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Proto->WriteRegister(RelayReg, 0);
}

TEST_F(TUnielProtocolTest, TestSetParam)
{
    EnqueueSetLowThreshold0Response();
    Proto->WriteRegister(ThresholdReg, 0x70);
}

TEST_F(TUnielProtocolTest, TestSetBrightness)
{
    EnqueueSetBrightnessResponse();
    Proto->WriteRegister(BrightnessReg, 0x42);
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

    Note() << "LoopOnce()";
    Observer->LoopOnce();
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

    Note() << "LoopOnce()";
    Observer->LoopOnce();
    SerialPort->DumpWhatWasRead();

    SerialPort->Close();
}
