#include "testlog.h"
#include "fake_serial_port.h"
#include "orion_device.h"
#include "orion_expectations.h"

class TOrionDeviceTest: public TSerialDeviceTest, public TOrionDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    POrionDevice Dev;

    PRegister RelayReg1;
    PRegister RelayReg2;
    PRegister RelayReg3;
    PRegister RelayReg4;
};

void TOrionDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    Dev = std::make_shared<TOrionDevice>(
        std::make_shared<TDeviceConfig>("orion", 0x01, "orion"),
        SerialPort,
        TSerialDeviceFactory::GetProtocol("orion"));

    RelayReg1 = TRegister::Intern(Dev, TRegisterConfig::Create(TOrionDevice::REG_RELAY, 0x01, U8));
    RelayReg2 = TRegister::Intern(Dev, TRegisterConfig::Create(TOrionDevice::REG_RELAY, 0x02, U8));
    RelayReg3 = TRegister::Intern(Dev, TRegisterConfig::Create(TOrionDevice::REG_RELAY, 0x03, U8));
    RelayReg4 = TRegister::Intern(Dev, TRegisterConfig::Create(TOrionDevice::REG_RELAY, 0x04, U8));

    SerialPort->Open();
}

void TOrionDeviceTest::TearDown()
{
    Dev.reset();
    SerialPort->DumpWhatWasRead();
    TSerialDeviceTest::TearDown();
}

TEST_F(TOrionDeviceTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    Dev->WriteRegister(RelayReg1, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Dev->WriteRegister(RelayReg1, 0);
}

class TOrionIntegrationTest: public TSerialDeviceIntegrationTest, public TOrionDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-orion-test.json"; }
};

void TOrionIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
}

void TOrionIntegrationTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialDeviceIntegrationTest::TearDown();
}
TEST_F(TOrionIntegrationTest, Poll)
{
    Observer->SetUp();
    ASSERT_TRUE(!!SerialPort);

    EnqueueSetRelayOnResponse();
    EnqueueSetRelay2On();
    EnqueueSetRelay3On2();

    Note() << "LoopOnce()";
    Observer->LoopOnce();
    SerialPort->DumpWhatWasRead();

    MQTTClient->DoPublish(true, 0, "/devices/pseudo_orion/controls/Relay 1/on", "1");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_orion/controls/Relay 2/on", "1");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_orion/controls/Relay 3/on", "2");

    Note() << "LoopOnce()";
    Observer->LoopOnce();
    SerialPort->DumpWhatWasRead();

    SerialPort->Close();
}
