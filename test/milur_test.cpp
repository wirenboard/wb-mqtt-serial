#include <string>
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include "milur_device.h"


class TMilurTest: public TSerialDeviceTest, public TMilurExpectations
{
protected:
    void SetUp();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

    PMilurDevice MilurDev;

    PVirtualRegister MilurPhaseAVoltageReg;
    PVirtualRegister MilurPhaseBVoltageReg;
    PVirtualRegister MilurPhaseCVoltageReg;

    PVirtualRegister MilurPhaseACurrentReg;
    PVirtualRegister MilurPhaseBCurrentReg;
	PVirtualRegister MilurPhaseCCurrentReg;

	PVirtualRegister MilurPhaseAActivePowerReg;
	PVirtualRegister MilurPhaseBActivePowerReg;
	PVirtualRegister MilurPhaseCActivePowerReg;
	PVirtualRegister MilurTotalActivePowerReg;

	PVirtualRegister MilurPhaseAReactivePowerReg;
	PVirtualRegister MilurPhaseBReactivePowerReg;
	PVirtualRegister MilurPhaseCReactivePowerReg;
	PVirtualRegister MilurTotalReactivePowerReg;

	PVirtualRegister MilurTotalConsumptionReg;
	PVirtualRegister MilurTotalReactiveEnergyReg;
	PVirtualRegister MilurFrequencyReg;
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

	MilurPhaseAVoltageReg = Reg(MilurDev, 100, TMilurDevice::REG_PARAM, U24);
	MilurPhaseBVoltageReg = Reg(MilurDev, 101, TMilurDevice::REG_PARAM, U24);
	MilurPhaseCVoltageReg = Reg(MilurDev, 102, TMilurDevice::REG_PARAM, U24);

	MilurPhaseACurrentReg = Reg(MilurDev, 103, TMilurDevice::REG_PARAM, U24);
	MilurPhaseBCurrentReg = Reg(MilurDev, 104, TMilurDevice::REG_PARAM, U24);
	MilurPhaseCCurrentReg = Reg(MilurDev, 105, TMilurDevice::REG_PARAM, U24);

	MilurPhaseAActivePowerReg = Reg(MilurDev, 106, TMilurDevice::REG_POWER, S32);
	MilurPhaseBActivePowerReg = Reg(MilurDev, 107, TMilurDevice::REG_POWER, S32);
	MilurPhaseCActivePowerReg = Reg(MilurDev, 108, TMilurDevice::REG_POWER, S32);
	MilurTotalActivePowerReg = Reg(MilurDev, 109, TMilurDevice::REG_POWER, S32);

	MilurPhaseAReactivePowerReg = Reg(MilurDev, 110, TMilurDevice::REG_POWER, S32);
	MilurPhaseBReactivePowerReg = Reg(MilurDev, 111, TMilurDevice::REG_POWER, S32);
	MilurPhaseCReactivePowerReg = Reg(MilurDev, 112, TMilurDevice::REG_POWER, S32);
	MilurTotalReactivePowerReg = Reg(MilurDev, 113, TMilurDevice::REG_POWER, S32);

	MilurTotalConsumptionReg = Reg(MilurDev, 118, TMilurDevice::REG_ENERGY, RBCD32);
	MilurTotalReactiveEnergyReg = Reg(MilurDev, 127, TMilurDevice::REG_ENERGY, U32);
	MilurFrequencyReg = Reg(MilurDev, 9, TMilurDevice::REG_FREQ, U16);

	SerialPort->Open();
}

void TMilurTest::VerifyParamQuery()
{
    auto MilurPhaseCVoltageRegQuery = GetReadQuery({ MilurPhaseCVoltageReg });
    auto MilurPhaseCCurrentRegQuery = GetReadQuery({ MilurPhaseCCurrentReg });
    auto MilurTotalConsumptionRegQuery = GetReadQuery({ MilurTotalConsumptionReg });
    auto MilurFrequencyRegQuery = GetReadQuery({ MilurFrequencyReg });

    EnqueueMilurPhaseCVoltageResponse();
    TestRead(MilurPhaseCVoltageRegQuery);
    ASSERT_EQ(std::to_string(0x03946f), MilurPhaseCVoltageReg->GetTextValue());

    EnqueueMilurPhaseCCurrentResponse();
    TestRead(MilurPhaseCCurrentRegQuery);
    ASSERT_EQ(std::to_string(0xffd8f0), MilurPhaseCCurrentReg->GetTextValue());

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    TestRead(MilurTotalConsumptionRegQuery);
    ASSERT_EQ(std::to_string(11144), MilurTotalConsumptionReg->GetTextValue());

    EnqueueMilurFrequencyResponse();
    TestRead(MilurFrequencyRegQuery);
    ASSERT_EQ(std::to_string(50080), MilurFrequencyReg->GetTextValue());
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
    auto MilurPhaseCVoltageRegQuery = GetReadQuery({ MilurPhaseCVoltageReg });

    EnqueueMilurSessionSetupResponse();
    EnqueueMilurNoSessionResponse();
    // reconnection
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    TestRead(MilurPhaseCVoltageRegQuery);
    ASSERT_EQ(std::to_string(0x03946f), MilurPhaseCVoltageReg->GetTextValue());
}

TEST_F(TMilurTest, Exception)
{
    auto MilurPhaseCVoltageRegQuery = GetReadQuery({ MilurPhaseCVoltageReg });

    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        MilurDev->Read(*MilurPhaseCVoltageRegQuery);
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

    PVirtualRegister MilurTotalConsumptionReg;
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
    MilurTotalConsumptionReg = Reg(MilurDev, 118, TMilurDevice::REG_ENERGY, RBCD32);

    SerialPort->Open();
}

void TMilur32Test::VerifyMilurQuery()
{
    auto MilurTotalConsumptionRegQuery = GetReadQuery({ MilurTotalConsumptionReg });

    EnqueueMilur32TotalConsumptionResponse();
    TestRead(MilurTotalConsumptionRegQuery);
    ASSERT_EQ(std::to_string(11144), MilurTotalConsumptionReg->GetTextValue());
}

TEST_F(TMilur32Test, MilurQuery)
{
    EnqueueMilur32SessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

//FIXME: ExpectNBytes() in Milur code isn't covered by tests here
