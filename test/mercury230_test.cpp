#include <string>
#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include "mercury230_device.h"
#include "memory_block.h"


class TMercury230Test: public TSerialDeviceTest, public TMercury230Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

    PMercury230Device Mercury230Dev;

    PVirtualRegister Mercury230TotalReactiveEnergyReg;
	PVirtualRegister Mercury230TotalConsumptionReg;
	PVirtualRegister Mercury230PReg;
	PVirtualRegister Mercury230P1Reg;
	PVirtualRegister Mercury230P2Reg;
	PVirtualRegister Mercury230P3Reg;
	PVirtualRegister Mercury230QReg;
	PVirtualRegister Mercury230Q1Reg;
	PVirtualRegister Mercury230Q2Reg;
	PVirtualRegister Mercury230Q3Reg;
	PVirtualRegister Mercury230U1Reg;
	PVirtualRegister Mercury230U2Reg;
	PVirtualRegister Mercury230U3Reg;
	PVirtualRegister Mercury230I1Reg;
	PVirtualRegister Mercury230I2Reg;
	PVirtualRegister Mercury230I3Reg;
	PVirtualRegister Mercury230FrequencyReg;
	PVirtualRegister Mercury230PFReg;
	PVirtualRegister Mercury230PF1Reg;
	PVirtualRegister Mercury230PF2Reg;
	PVirtualRegister Mercury230PF3Reg;
	PVirtualRegister Mercury230KU1Reg;
	PVirtualRegister Mercury230KU2Reg;
	PVirtualRegister Mercury230KU3Reg;
	PVirtualRegister Mercury230TempReg;
};

PDeviceConfig TMercury230Test::GetDeviceConfig()
{
	return std::make_shared<TDeviceConfig>("mercury230", std::to_string(0x00), "mercury230");
}

void TMercury230Test::SetUp()
{
	TSerialDeviceTest::SetUp();

	Mercury230Dev = std::make_shared<TMercury230Device>(GetDeviceConfig(), SerialPort,
	                            TSerialDeviceFactory::GetProtocol("mercury230"));
	Mercury230TotalConsumptionReg = RegValue(Mercury230Dev, 0x00, 0, TMercury230Device::REG_VALUE_ARRAY, U32);
	Mercury230TotalReactiveEnergyReg = RegValue(Mercury230Dev, 0x00, 2, TMercury230Device::REG_VALUE_ARRAY, U32);

	Mercury230PReg  = Reg(Mercury230Dev, 0x1100, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230P1Reg = Reg(Mercury230Dev, 0x1101, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230P2Reg = Reg(Mercury230Dev, 0x1102, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230P3Reg = Reg(Mercury230Dev, 0x1103, TMercury230Device::REG_PARAM_SIGN_ACT, S24);

	Mercury230QReg = Reg(Mercury230Dev, 0x1104, TMercury230Device::REG_PARAM_SIGN_REACT, S24);
	Mercury230Q1Reg = Reg(Mercury230Dev, 0x1105, TMercury230Device::REG_PARAM_SIGN_REACT, S24);
	Mercury230Q2Reg = Reg(Mercury230Dev, 0x1106, TMercury230Device::REG_PARAM_SIGN_REACT, S24);
	Mercury230Q3Reg = Reg(Mercury230Dev, 0x1107, TMercury230Device::REG_PARAM_SIGN_REACT, S24);

	Mercury230U1Reg = Reg(Mercury230Dev, 0x1111, TMercury230Device::REG_PARAM, U24);
	Mercury230U2Reg = Reg(Mercury230Dev, 0x1112, TMercury230Device::REG_PARAM, U24);
	Mercury230U3Reg = Reg(Mercury230Dev, 0x1113, TMercury230Device::REG_PARAM, U24);

	Mercury230I1Reg = Reg(Mercury230Dev, 0x1121, TMercury230Device::REG_PARAM, U24);
	Mercury230I2Reg = Reg(Mercury230Dev, 0x1122, TMercury230Device::REG_PARAM, U24);
	Mercury230I3Reg = Reg(Mercury230Dev, 0x1123, TMercury230Device::REG_PARAM, U24);

	Mercury230FrequencyReg = Reg(Mercury230Dev, 0x1140, TMercury230Device::REG_PARAM, U24);

	Mercury230PFReg = Reg(Mercury230Dev, 0x1130, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230PF1Reg = Reg(Mercury230Dev, 0x1131, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230PF2Reg = Reg(Mercury230Dev, 0x1132, TMercury230Device::REG_PARAM_SIGN_ACT, S24);
	Mercury230PF3Reg = Reg(Mercury230Dev, 0x1133, TMercury230Device::REG_PARAM_SIGN_ACT, S24);

	Mercury230KU1Reg = Reg(Mercury230Dev, 0x1161, TMercury230Device::REG_PARAM, U16);
	Mercury230KU2Reg = Reg(Mercury230Dev, 0x1162, TMercury230Device::REG_PARAM, U16);
	Mercury230KU3Reg = Reg(Mercury230Dev, 0x1163, TMercury230Device::REG_PARAM, U16);

	Mercury230TempReg = Reg(Mercury230Dev, 0x1170, TMercury230Device::REG_PARAM_BE, U16);


	SerialPort->Open();
}

TEST_F(TMercury230Test, ReadEnergy)
{
    auto Mercury230ValArrayMBQuery = GetReadQuery({ Mercury230TotalConsumptionReg, Mercury230TotalReactiveEnergyReg });

    ASSERT_EQ(1, Mercury230ValArrayMBQuery->GetBlockCount());

    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230EnergyResponse1();

    // Register address for energy arrays:
    // 0000 0000 CCCC CCCC TTTT AAAA MMMM IIII
    // C = command (0x05)
    // A = array number
    // M = month
    // T = tariff (FIXME!!! 5 values)
    // I = index
    // Note: for A=6, 12-byte and not 16-byte value is returned.
    // This is not supported at the moment.

    // Here we make sure that consecutive requests querying the same array
    // don't cause redundant requests during the single poll cycle.

    TestRead(Mercury230ValArrayMBQuery);
    ASSERT_EQ(3196200, Mercury230TotalConsumptionReg->GetValue());
    ASSERT_EQ(300444,  Mercury230TotalReactiveEnergyReg->GetValue());
    Mercury230Dev->EndPollCycle();

    EnqueueMercury230EnergyResponse2();
    TestRead(Mercury230ValArrayMBQuery);
    ASSERT_EQ(3196201, Mercury230TotalConsumptionReg->GetValue());
    ASSERT_EQ(300445,  Mercury230TotalReactiveEnergyReg->GetValue());
    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

void TMercury230Test::VerifyParamQuery()
{
    auto Mercury230U1RegQuery = GetReadQuery({ Mercury230U1Reg });
    auto Mercury230I1RegQuery = GetReadQuery({ Mercury230I1Reg });
    auto Mercury230I2RegQuery = GetReadQuery({ Mercury230I2Reg });
    auto Mercury230U2RegQuery = GetReadQuery({ Mercury230U2Reg });
    auto Mercury230U3RegQuery = GetReadQuery({ Mercury230U3Reg });
    auto Mercury230PRegQuery = GetReadQuery({ Mercury230PReg });
    auto Mercury230TempRegQuery = GetReadQuery({ Mercury230TempReg });

    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    TestRead(Mercury230U1RegQuery);
    ASSERT_EQ(24128, Mercury230U1Reg->GetValue());

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    TestRead(Mercury230I1RegQuery);
    ASSERT_EQ(69, Mercury230I1Reg->GetValue());

    EnqueueMercury230I2Response();
    // subparam 0x22 = current (phase 2)
    TestRead(Mercury230I2RegQuery);
    ASSERT_EQ(96, Mercury230I2Reg->GetValue());

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    TestRead(Mercury230U2RegQuery);
    ASSERT_EQ(24043, Mercury230U2Reg->GetValue());

    EnqueueMercury230U3Response();
	// subparam 0x12 = voltage (phase 3)
    TestRead(Mercury230U3RegQuery);
	ASSERT_EQ(50405, Mercury230U3Reg->GetValue());

    EnqueueMercury230PResponse();
    // Total power (P)
    TestRead(Mercury230PRegQuery);
    ASSERT_EQ(553095, Mercury230PReg->GetValue());

    EnqueueMercury230TempResponse();
    TestRead(Mercury230TempRegQuery);
    ASSERT_EQ(24, Mercury230TempReg->GetValue());
}

TEST_F(TMercury230Test, ReadParams)
{
    EnqueueMercury230SessionSetupResponse();
    VerifyParamQuery();
    Mercury230Dev->EndPollCycle();
    VerifyParamQuery();
    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TMercury230Test, Reconnect)
{
    auto Mercury230U2RegQuery = GetReadQuery({ Mercury230U2Reg });

    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230NoSessionResponse();
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230U2Response();

    // subparam 0x12 = voltage (phase 2)
    TestRead(Mercury230U2RegQuery);
    ASSERT_EQ(24043, Mercury230U2Reg->GetValue());

    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TMercury230Test, Exception)
{
    auto Mercury230U2RegQuery = GetReadQuery({ Mercury230U2Reg });

    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        Mercury230Dev->Read(*Mercury230U2RegQuery);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: Internal meter error", e.what());
        SerialPort->Close();
    }
}


class TMercury230CustomPasswordTest : public TMercury230Test {
public:

    PDeviceConfig GetDeviceConfig() override;
};

PDeviceConfig TMercury230CustomPasswordTest::GetDeviceConfig()
{
    PDeviceConfig device_config = TMercury230Test::GetDeviceConfig();
    device_config->Password = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    device_config->AccessLevel = 2;
    return device_config;
}

TEST_F(TMercury230CustomPasswordTest, Test)
{
    EnqueueMercury230AccessLevel2SessionSetupResponse();
    VerifyParamQuery();
    Mercury230Dev->EndPollCycle();

    SerialPort->Close();
}


class TMercury230IntegrationTest: public TSerialDeviceIntegrationTest, public TMercury230Expectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "configs/config-mercury230-test.json"; }
    const char* GetTemplatePath() const { return "../wb-mqtt-serial-templates/"; }
    void ExpectQueries(bool firstPoll);
};

void TMercury230IntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(150));
    ASSERT_TRUE(!!SerialPort);
}

void TMercury230IntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TMercury230IntegrationTest::ExpectQueries(bool firstPoll)
{
	if (firstPoll) {
		EnqueueMercury230SessionSetupResponse();
	}

	EnqueueMercury230EnergyResponse1();

    EnqueueMercury230PerPhaseEnergyResponse();

    EnqueueMercury230PResponse();
	EnqueueMercury230P1Response();
	EnqueueMercury230P2Response();
	EnqueueMercury230P3Response();

    EnqueueMercury230QResponse();
	EnqueueMercury230Q1Response();
	EnqueueMercury230Q2Response();
	EnqueueMercury230Q3Response();

	EnqueueMercury230U1Response();
	EnqueueMercury230U2Response();
	EnqueueMercury230U3Response();

	EnqueueMercury230I1Response();
	EnqueueMercury230I2Response();
	EnqueueMercury230I3Response();

	EnqueueMercury230FrequencyResponse();

    EnqueueMercury230PFResponse();
	EnqueueMercury230PF1Response();
	EnqueueMercury230PF2Response();
	EnqueueMercury230PF3Response();

	EnqueueMercury230KU1Response();
	EnqueueMercury230KU2Response();
	EnqueueMercury230KU3Response();

	EnqueueMercury230TempResponse();
}

TEST_F(TMercury230IntegrationTest, Poll)
{
	ExpectQueries(true);
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

// NOTE: max unchanged interval tests concern the whole driver,
// not just EM case, but it's hard to test for modbus devices
// because pty_based_fake_serial has to be used there.

TEST_F(TMercury230IntegrationTest, MaxUnchangedInterval) {
    for (int i = 0; i < 6; ++i) {
        if (i == 2 || i == 5)
            SerialPort->Elapse(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(100))); // force 'unchanged' update
        ExpectQueries(i == 0);

        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}

TEST_F(TMercury230IntegrationTest, ZeroMaxUnchangedInterval) {
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
