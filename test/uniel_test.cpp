#include "devices/uniel_device.h"
#include "fake_serial_port.h"
#include "uniel_expectations.h"
#include <string>
#include <wblib/testing/testlog.h>

class TUnielDeviceTest: public TSerialDeviceTest, public TUnielDeviceExpectations
{
protected:
    void SetUp();
    void TearDown();
    PUnielDevice Dev;

    PRegister InputReg;
    PRegister RelayReg;
    PRegister ThresholdReg;
    PRegister BrightnessReg;
};

void TUnielDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    Dev = std::make_shared<TUnielDevice>(std::make_shared<TDeviceConfig>("uniel", std::to_string(0x01), "uniel"),
                                         SerialPort,
                                         DeviceFactory.GetProtocol("uniel"));

    InputReg = Dev->AddRegister(TRegisterConfig::Create(TUnielDevice::REG_INPUT, 0x0a, U8));
    RelayReg = Dev->AddRegister(TRegisterConfig::Create(TUnielDevice::REG_RELAY, 0x1b, U8));
    ThresholdReg = Dev->AddRegister(TRegisterConfig::Create(TUnielDevice::REG_PARAM, 0x02, U8));
    BrightnessReg = Dev->AddRegister(TRegisterConfig::Create(TUnielDevice::REG_BRIGHTNESS, 0x141, U8));

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
    ASSERT_EQ(TRegisterValue{154}, Dev->ReadRegisterImpl(InputReg));

    // TBD: rm (dupe)
    SerialPort->DumpWhatWasRead();
    EnqueueVoltageQueryResponse();
    ASSERT_EQ(TRegisterValue{154}, Dev->ReadRegisterImpl(InputReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOffQueryResponse();
    ASSERT_EQ(TRegisterValue{0}, Dev->ReadRegisterImpl(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueRelayOnQueryResponse();
    ASSERT_EQ(TRegisterValue{1}, Dev->ReadRegisterImpl(RelayReg));

    SerialPort->DumpWhatWasRead();
    EnqueueThreshold0QueryResponse();
    ASSERT_EQ(TRegisterValue{0x70}, Dev->ReadRegisterImpl(ThresholdReg));

    SerialPort->DumpWhatWasRead();
    EnqueueBrightnessQueryResponse();
    ASSERT_EQ(TRegisterValue{66}, Dev->ReadRegisterImpl(BrightnessReg));
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

class TUnielIntegrationTest: public TSerialDeviceIntegrationTest, public TUnielDeviceExpectations
{
protected:
    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
    {
        return "configs/config-uniel-test.json";
    }
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
    ASSERT_TRUE(!!SerialPort);

    EnqueueRelayOffQueryResponse();
    EnqueueVoltageQueryResponse();
    EnqueueThreshold0QueryResponse();
    EnqueueBrightnessQueryResponse();

    Note() << "LoopOnce()";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }

    PublishWaitOnValue("/devices/pseudo_uniel/controls/Relay 1/on", "1");
    PublishWaitOnValue("/devices/pseudo_uniel/controls/LowThr/on", "112");
    PublishWaitOnValue("/devices/pseudo_uniel/controls/LED 1/on", "66");

    EnqueueSetRelayOnResponse();
    EnqueueSetLowThreshold0Response();
    EnqueueSetBrightnessResponse();

    EnqueueRelayOnQueryResponse();
    EnqueueVoltageQueryResponse();
    EnqueueThreshold0QueryResponse();
    EnqueueBrightnessQueryResponse();

    Note() << "LoopOnce()";
    for (auto i = 0; i < 4; ++i) {
        SerialDriver->LoopOnce();
        SerialPort->DumpWhatWasRead();
    }
    SerialPort->Close();
}
