#include <string>
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include "milur_device.h"
#include "protocol_register.h"


class TMilurTest: public TSerialDeviceTest, public TMilurExpectations
{
protected:
    void SetUp();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

    PMilurDevice MilurDev;

    PProtocolRegister MilurPhaseAVoltageReg;
    PProtocolRegister MilurPhaseBVoltageReg;
    PProtocolRegister MilurPhaseCVoltageReg;

    PProtocolRegister MilurPhaseACurrentReg;
    PProtocolRegister MilurPhaseBCurrentReg;
	PProtocolRegister MilurPhaseCCurrentReg;

	PProtocolRegister MilurPhaseAActivePowerReg;
	PProtocolRegister MilurPhaseBActivePowerReg;
	PProtocolRegister MilurPhaseCActivePowerReg;
	PProtocolRegister MilurTotalActivePowerReg;

	PProtocolRegister MilurPhaseAReactivePowerReg;
	PProtocolRegister MilurPhaseBReactivePowerReg;
	PProtocolRegister MilurPhaseCReactivePowerReg;
	PProtocolRegister MilurTotalReactivePowerReg;

	PProtocolRegister MilurTotalConsumptionReg;
	PProtocolRegister MilurTotalReactiveEnergyReg;
	PProtocolRegister MilurFrequencyReg;
};

PDeviceConfig TMilurTest::GetDeviceConfig()
{
	return std::make_shared<TDeviceConfig>("milur", std::to_string(0xff), "milur");
}

void TMilurTest::SetUp()
{
	TSerialDeviceTest::SetUp();

	MilurDev = std::make_shared<TMilurDevice>(GetDeviceConfig(), SerialPort,
	                            TSerialDeviceFactory::GetProtocol("milur"));

	MilurPhaseAVoltageReg = MilurDev->GetCreateRegister(100, TMilurDevice::REG_PARAM);
	MilurPhaseBVoltageReg = MilurDev->GetCreateRegister(101, TMilurDevice::REG_PARAM);
	MilurPhaseCVoltageReg = MilurDev->GetCreateRegister(102, TMilurDevice::REG_PARAM);

	MilurPhaseACurrentReg = MilurDev->GetCreateRegister(103, TMilurDevice::REG_PARAM);
	MilurPhaseBCurrentReg = MilurDev->GetCreateRegister(104, TMilurDevice::REG_PARAM);
	MilurPhaseCCurrentReg = MilurDev->GetCreateRegister(105, TMilurDevice::REG_PARAM);

	MilurPhaseAActivePowerReg = MilurDev->GetCreateRegister(106, TMilurDevice::REG_POWER);
	MilurPhaseBActivePowerReg = MilurDev->GetCreateRegister(107, TMilurDevice::REG_POWER);
	MilurPhaseCActivePowerReg = MilurDev->GetCreateRegister(108, TMilurDevice::REG_POWER);
	MilurTotalActivePowerReg = MilurDev->GetCreateRegister(109, TMilurDevice::REG_POWER);

	MilurPhaseAReactivePowerReg = MilurDev->GetCreateRegister(110, TMilurDevice::REG_POWER);
	MilurPhaseBReactivePowerReg = MilurDev->GetCreateRegister(111, TMilurDevice::REG_POWER);
	MilurPhaseCReactivePowerReg = MilurDev->GetCreateRegister(112, TMilurDevice::REG_POWER);
	MilurTotalReactivePowerReg = MilurDev->GetCreateRegister(113, TMilurDevice::REG_POWER);

	MilurTotalConsumptionReg = MilurDev->GetCreateRegister(118, TMilurDevice::REG_ENERGY);
	MilurTotalReactiveEnergyReg = MilurDev->GetCreateRegister(127, TMilurDevice::REG_ENERGY);
	MilurFrequencyReg = MilurDev->GetCreateRegister(9, TMilurDevice::REG_FREQ);

	SerialPort->Open();
}

void TMilurTest::VerifyParamQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurDev->ReadProtocolRegister(MilurPhaseCVoltageReg));

    EnqueueMilurPhaseCCurrentResponse();
    ASSERT_EQ(0xffd8f0, MilurDev->ReadProtocolRegister(MilurPhaseCCurrentReg));

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    ASSERT_EQ(0x11144, MilurDev->ReadProtocolRegister(MilurTotalConsumptionReg));

    EnqueueMilurFrequencyResponse();
    ASSERT_EQ(50080, MilurDev->ReadProtocolRegister(MilurFrequencyReg));
}

TEST_F(TMilurTest, Query)
{
    EnqueueMilurSessionSetupResponse();
    VerifyParamQuery();
    VerifyParamQuery();
    SerialPort->Close();
}

TEST_F(TMilurTest, Reconnect)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurNoSessionResponse();
    // reconnection
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurDev->ReadProtocolRegister(MilurPhaseCVoltageReg));
}

TEST_F(TMilurTest, Exception)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        MilurDev->ReadProtocolRegister(MilurPhaseCVoltageReg);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: EEPROM access error", e.what());
        SerialPort->Close();
    }
}

class TMilurCustomPasswordTest : public TMilurTest {
public:
    PDeviceConfig GetDeviceConfig();
};

PDeviceConfig TMilurCustomPasswordTest::GetDeviceConfig()
{
    PDeviceConfig device_config = TMilurTest::GetDeviceConfig();
    device_config->Password = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    device_config->AccessLevel = 2;
    return device_config;
}

TEST_F(TMilurCustomPasswordTest, Test)
{
    EnqueueMilurAccessLevel2SessionSetupResponse();
    VerifyParamQuery();
    MilurDev->EndPollCycle();

    SerialPort->Close();
}


class TMilurIntegrationTest: public TSerialDeviceIntegrationTest, public TMilurExpectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "configs/config-milur-test.json"; }
    const char* GetTemplatePath() const { return "../wb-mqtt-serial-templates/"; }
    void ExpectQueries(bool firstPoll);
};

void TMilurIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(150));
    ASSERT_TRUE(!!SerialPort);
}

void TMilurIntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TMilurIntegrationTest::ExpectQueries(bool firstPoll)
{
    if (firstPoll) {
    	EnqueueMilurIgnoredPacketWorkaround();
        EnqueueMilurSessionSetupResponse();
    }
    EnqueueMilurPhaseAVoltageResponse();
    EnqueueMilurPhaseBVoltageResponse();
    EnqueueMilurPhaseCVoltageResponse();

    EnqueueMilurPhaseACurrentResponse();
    EnqueueMilurPhaseBCurrentResponse();
    EnqueueMilurPhaseCCurrentResponse();

    EnqueueMilurPhaseAActivePowerResponse();
    EnqueueMilurPhaseBActivePowerResponse();
    EnqueueMilurPhaseCActivePowerResponse();
    EnqueueMilurTotalActivePowerResponse();

    EnqueueMilurPhaseAReactivePowerResponse();
    EnqueueMilurPhaseBReactivePowerResponse();
    EnqueueMilurPhaseCReactivePowerResponse();
    EnqueueMilurTotalReactivePowerResponse();

    EnqueueMilurTotalConsumptionResponse();
    EnqueueMilurTotalReactiveEnergyResponse();

    EnqueueMilurFrequencyResponse();
}

TEST_F(TMilurIntegrationTest, Poll)
{
	ExpectQueries(true);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

// NOTE: max unchanged interval tests concern the whole driver,
// not just EM case, but it's hard to test for modbus devices
// because pty_based_fake_serial has to be used there.

TEST_F(TMilurIntegrationTest, MaxUnchangedInterval) {
    for (int i = 0; i < 6; ++i) {
        if (i == 2 || i == 5)
            SerialPort->Elapse(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(100))); // force 'unchanged' update
        ExpectQueries(i == 0);

        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}

TEST_F(TMilurIntegrationTest, ZeroMaxUnchangedInterval) {
    // Patching config after the driver is initialized is unpretty,
    // but adding separate fixture for this test case due to config
    // changes would be even uglier.
    Config->MaxUnchangedInterval = 0;
    for (auto portConfig: Config->PortConfigs)
        portConfig->MaxUnchangedInterval = 0;

    for (int i = 0; i < 3; ++i) {
        ExpectQueries(i == 0);
        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}

class TMilur32Test: public TSerialDeviceTest, public TMilurExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    virtual PDeviceConfig MilurConfig();
    PMilurDevice MilurDev;

    PProtocolRegister MilurTotalConsumptionReg;
};

PDeviceConfig TMilur32Test::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", std::to_string(49932), "milur");
}

void TMilur32Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort,
                            TSerialDeviceFactory::GetProtocol("milur"));
    MilurTotalConsumptionReg = MilurDev->GetCreateRegister(118, TMilurDevice::REG_ENERGY);

    SerialPort->Open();
}

void TMilur32Test::VerifyMilurQuery()
{
    EnqueueMilur32TotalConsumptionResponse();
    ASSERT_EQ(0x11144, MilurDev->ReadProtocolRegister(MilurTotalConsumptionReg));
}

TEST_F(TMilur32Test, MilurQuery)
{
    EnqueueMilur32SessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

//FIXME: ExpectNBytes() in Milur code isn't covered by tests here
