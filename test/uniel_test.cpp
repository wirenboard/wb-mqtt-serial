#include <string>
#include "testlog.h"
#include "fake_serial_port.h"
#include "uniel_device.h"
#include "uniel_expectations.h"

class TUnielDeviceTest: public TSerialDeviceTest, public TUnielDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    PUnielDevice Dev;

    PVirtualRegister InputReg;
    PVirtualRegister RelayReg;
    PVirtualRegister ThresholdReg;
    PVirtualRegister BrightnessReg;
};

void TUnielDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    Dev = std::make_shared<TUnielDevice>(
        std::make_shared<TDeviceConfig>("uniel", std::to_string(0x01), "uniel"),
        SerialPort,
        TSerialDeviceFactory::GetProtocol("uniel"));

    InputReg = Reg(Dev, 0x0a, TUnielDevice::REG_INPUT, U8);
    RelayReg = Reg(Dev, 0x1b, TUnielDevice::REG_RELAY, U8);
    ThresholdReg = Reg(Dev, 0x02, TUnielDevice::REG_PARAM, U8);
    BrightnessReg = Reg(Dev, 0x141, TUnielDevice::REG_BRIGHTNESS, U8);

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
    TestRead(InputRegQuery);
    ASSERT_EQ(154, InputReg->GetValue());

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    TestRead(InputRegQuery);
    ASSERT_EQ(154, InputReg->GetValue());

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    TestRead(RelayRegQuery);
    ASSERT_EQ(0, RelayReg->GetValue());

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    TestRead(RelayRegQuery);
    ASSERT_EQ(1, RelayReg->GetValue());

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    TestRead(ThresholdRegQuery);
    ASSERT_EQ(0x70, ThresholdReg->GetValue());

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    TestRead(BrightnessRegQuery);
    ASSERT_EQ(66, BrightnessReg->GetValue());
}

TEST_F(TUnielDeviceTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    RelayReg->SetValue(1);
    RelayReg->Flush();

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    RelayReg->SetValue(0);
    RelayReg->Flush();
}

TEST_F(TUnielDeviceTest, TestSetParam)
{
    EnqueueSetLowThreshold0Response();
    ThresholdReg->SetValue(0x70);
    ThresholdReg->Flush();
}

TEST_F(TUnielDeviceTest, TestSetBrightness)
{
    EnqueueSetBrightnessResponse();
    BrightnessReg->SetValue(0x42);
    BrightnessReg->Flush();
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
