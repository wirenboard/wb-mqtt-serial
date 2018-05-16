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

    InputReg = Dev->GetCreateMemoryBlock(0x0a, TUnielDevice::REG_INPUT);
    RelayReg = Dev->GetCreateMemoryBlock(0x1b, TUnielDevice::REG_RELAY);
    ThresholdReg = Dev->GetCreateMemoryBlock(0x02, TUnielDevice::REG_PARAM);
    BrightnessReg = Dev->GetCreateMemoryBlock(0x141, TUnielDevice::REG_BRIGHTNESS);

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
    auto InputRegQuery      = GetReadQuery({ InputReg });
    auto RelayRegQuery      = GetReadQuery({ RelayReg });
    auto ThresholdRegQuery  = GetReadQuery({ ThresholdReg });
    auto BrightnessRegQuery = GetReadQuery({ BrightnessReg });

    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, TestRead(InputRegQuery)[0]);

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(154, TestRead(InputRegQuery)[0]);

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    ASSERT_EQ(0, TestRead(RelayRegQuery)[0]);

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    ASSERT_EQ(1, TestRead(RelayRegQuery)[0]);

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    ASSERT_EQ(0x70, TestRead(ThresholdRegQuery)[0]);

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    ASSERT_EQ(66, TestRead(BrightnessRegQuery)[0]);
}

TEST_F(TUnielDeviceTest, TestSetRelayState)
{
    auto RelayRegQuery = GetWriteQuery({ RelayReg });
    const auto & descs = GetValueDescs({ RelayReg });

    assert(descs.size() == 1);

    EnqueueSetRelayOnResponse();
    TestWrite(RelayRegQuery, descs[0], 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    TestWrite(RelayRegQuery, descs[0], 0);
}

TEST_F(TUnielDeviceTest, TestSetParam)
{
    auto ThresholdRegQuery = GetWriteQuery({ ThresholdReg });
    const auto & descs     = GetValueDescs({ ThresholdReg });

    EnqueueSetLowThreshold0Response();
    TestWrite(ThresholdRegQuery, descs[0], 0x70);
}

TEST_F(TUnielDeviceTest, TestSetBrightness)
{
    auto BrightnessRegQuery = GetWriteQuery({ BrightnessReg });
    const auto & descs      = GetValueDescs({ BrightnessReg });

    EnqueueSetBrightnessResponse();
    TestWrite(BrightnessRegQuery, descs[0], 0x42);
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
