#include <string>
#include "testlog.h"
#include "fake_serial_port.h"
#include "s2k_device.h"
#include "s2k_expectations.h"
#include "protocol_register.h"

class TS2KDeviceTest: public TSerialDeviceTest, public TS2KDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    PS2KDevice Dev;

    PMemoryBlock RelayReg1;
    PMemoryBlock RelayReg2;
    PMemoryBlock RelayReg3;
    PMemoryBlock RelayReg4;
};

void TS2KDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    Dev = std::make_shared<TS2KDevice>(
        std::make_shared<TDeviceConfig>("s2k", std::to_string(0x01), "s2k"),
        SerialPort,
        TSerialDeviceFactory::GetProtocol("s2k"));

    RelayReg1 = Dev->GetCreateRegister(0x01, TS2KDevice::REG_RELAY);
    RelayReg2 = Dev->GetCreateRegister(0x02, TS2KDevice::REG_RELAY);
    RelayReg3 = Dev->GetCreateRegister(0x03, TS2KDevice::REG_RELAY);
    RelayReg4 = Dev->GetCreateRegister(0x04, TS2KDevice::REG_RELAY);

    SerialPort->Open();
}

void TS2KDeviceTest::TearDown()
{
    Dev.reset();
    SerialPort->DumpWhatWasRead();
    TSerialDeviceTest::TearDown();
}

TEST_F(TS2KDeviceTest, TestSetRelayState)
{
    EnqueueSetRelayOnResponse();
    Dev->WriteMemoryBlock(RelayReg1, 1);

    SerialPort->DumpWhatWasRead();
    EnqueueSetRelayOffResponse();
    Dev->WriteMemoryBlock(RelayReg1, 0);
}

class TS2KIntegrationTest: public TSerialDeviceIntegrationTest, public TS2KDeviceExpectations {
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-s2k-test.json"; }
};

void TS2KIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
}

void TS2KIntegrationTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialDeviceIntegrationTest::TearDown();
}
TEST_F(TS2KIntegrationTest, Poll)
{
    Observer->SetUp();
    ASSERT_TRUE(!!SerialPort);

    EnqueueReadConfig1();
    EnqueueReadConfig2();
    EnqueueReadConfig3();
    EnqueueReadConfig4();
    EnqueueReadConfig5();
    EnqueueReadConfig6();
    EnqueueReadConfig7();
    EnqueueReadConfig8();

    Note() << "LoopOnce()";
    Observer->LoopOnce();
    SerialPort->DumpWhatWasRead();

    EnqueueSetRelayOnResponse();
    EnqueueSetRelay2On();
    EnqueueSetRelay3On2();

    MQTTClient->DoPublish(true, 0, "/devices/pseudo_s2k/controls/Relay 1/on", "1");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_s2k/controls/Relay 2/on", "1");
    MQTTClient->DoPublish(true, 0, "/devices/pseudo_s2k/controls/Relay 3/on", "2");
    SerialPort->DumpWhatWasRead();


    EnqueueReadConfig1();
    EnqueueReadConfig2();
    EnqueueReadConfig3();
    EnqueueReadConfig4();
    EnqueueReadConfig5();
    EnqueueReadConfig6();
    EnqueueReadConfig7();
    EnqueueReadConfig8();

    Note() << "LoopOnce()";
    Observer->LoopOnce();
    SerialPort->DumpWhatWasRead();

    SerialPort->Close();
}
