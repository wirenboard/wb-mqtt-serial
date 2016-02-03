#include "testlog.h"
#include "fake_serial_port.h"
#include "uniel_device.h"
#include "uniel_expectations.h"

namespace {
    PRegister InputReg(new TRegister(1, TUnielDevice::REG_INPUT, 0x0a, U8));
    PRegister RelayReg(new TRegister(1, TUnielDevice::REG_RELAY, 0x1b, U8));
    PRegister ThresholdReg(new TRegister(1, TUnielDevice::REG_PARAM, 0x02, U8));
    PRegister BrightnessReg(new TRegister(1, TUnielDevice::REG_BRIGHTNESS, 0x141, U8));
};

class TUnielDeviceTest: public TSerialDeviceTest, public TUnielDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    PUnielDevice Dev;
};

void TUnielDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    Dev = std::make_shared<TUnielDevice>(
        std::make_shared<TDeviceConfig>("uniel", 0x01, "uniel"),
        SerialPort);
    SerialPort->Open();
}

void TUnielDeviceTest::TearDown()
{
    Dev.reset();
    SerialPort->DumpWhatWasRead();
    TSerialDeviceTest::TearDown();
}

TEST_F(TUnielDeviceTest, TestQuery)
{
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, Dev->ReadRegister(InputReg));

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, Dev->ReadRegister(InputReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    ASSERT_EQ(0, Dev->ReadRegister(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    ASSERT_EQ(1, Dev->ReadRegister(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    ASSERT_EQ(0x70, Dev->ReadRegister(ThresholdReg));

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    ASSERT_EQ(66, Dev->ReadRegister(BrightnessReg));
}

TEST_F(TUnielDeviceTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    Dev->WriteRegister(RelayReg, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Dev->WriteRegister(RelayReg, 0);
}

TEST_F(TUnielDeviceTest, TestSetParam)
{
    EnqueueSetLowThreshold0Response();
    Dev->WriteRegister(ThresholdReg, 0x70);
}

TEST_F(TUnielDeviceTest, TestSetBrightness)
{
    EnqueueSetBrightnessResponse();
    Dev->WriteRegister(BrightnessReg, 0x42);
}

class TUnielIntegrationTest: public TSerialDeviceIntegrationTest, public TUnielDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-uniel-test.json"; }
};

void TUnielIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
}

void TUnielIntegrationTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialDeviceIntegrationTest::TearDown();
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
