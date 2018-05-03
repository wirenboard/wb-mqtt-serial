#include <string>
#include "testlog.h"
#include "fake_serial_port.h"
#include "uniel_device.h"
#include "uniel_expectations.h"
#include "memory_block.h"

class TUnielDeviceTest: public TSerialDeviceTest, public TUnielDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    PUnielDevice Dev;

    PMemoryBlock InputReg;
    PMemoryBlock RelayReg;
    PMemoryBlock ThresholdReg;
    PMemoryBlock BrightnessReg;
};

void TUnielDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    Dev = std::make_shared<TUnielDevice>(
        std::make_shared<TDeviceConfig>("uniel", std::to_string(0x01), "uniel"),
        SerialPort,
        TSerialDeviceFactory::GetProtocol("uniel"));

    InputReg = Dev->GetCreateRegister(0x0a, TUnielDevice::REG_INPUT);
    RelayReg = Dev->GetCreateRegister(0x1b, TUnielDevice::REG_RELAY);
    ThresholdReg = Dev->GetCreateRegister(0x02, TUnielDevice::REG_PARAM);
    BrightnessReg = Dev->GetCreateRegister(0x141, TUnielDevice::REG_BRIGHTNESS);

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
    ASSERT_EQ(154, Dev->ReadMemoryBlock(InputReg));

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, Dev->ReadMemoryBlock(InputReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    ASSERT_EQ(0, Dev->ReadMemoryBlock(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    ASSERT_EQ(1, Dev->ReadMemoryBlock(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    ASSERT_EQ(0x70, Dev->ReadMemoryBlock(ThresholdReg));

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    ASSERT_EQ(66, Dev->ReadMemoryBlock(BrightnessReg));
}

TEST_F(TUnielDeviceTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    Dev->WriteMemoryBlock(RelayReg, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Dev->WriteMemoryBlock(RelayReg, 0);
}

TEST_F(TUnielDeviceTest, TestSetParam)
{
    EnqueueSetLowThreshold0Response();
    Dev->WriteMemoryBlock(ThresholdReg, 0x70);
}

TEST_F(TUnielDeviceTest, TestSetBrightness)
{
    EnqueueSetBrightnessResponse();
    Dev->WriteMemoryBlock(BrightnessReg, 0x42);
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
