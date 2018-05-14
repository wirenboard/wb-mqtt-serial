#include <string>
#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include "mercury230_device.h"
#include "memory_block.h"
#include "virtual_register.h"


namespace
{
    PMemoryBlock GetMemoryBlock(const PVirtualRegister & reg)
    {
        const auto & memoryBlocks = reg->GetMemoryBlocks();

        assert(memoryBlocks.size() == 1);

        return *memoryBlocks.begin();
    }
}


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
	Mercury230TotalConsumptionReg =
		TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0000, U32), Mercury230Dev);
	Mercury230TotalReactiveEnergyReg =
		TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0002, U32), Mercury230Dev);

	Mercury230PReg  = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1100, S24), Mercury230Dev);
	Mercury230P1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1101, S24), Mercury230Dev);
	Mercury230P2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1102, S24), Mercury230Dev);
	Mercury230P3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1103, S24), Mercury230Dev);

	Mercury230QReg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1104, S24), Mercury230Dev);
	Mercury230Q1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1105, S24), Mercury230Dev);
	Mercury230Q2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1106, S24), Mercury230Dev);
	Mercury230Q3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1107, S24), Mercury230Dev);

	Mercury230U1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1111, U24), Mercury230Dev);
	Mercury230U2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1112, U24), Mercury230Dev);
	Mercury230U3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1113, U24), Mercury230Dev);

	Mercury230I1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1121, U24), Mercury230Dev);
	Mercury230I2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1122, U24), Mercury230Dev);
	Mercury230I3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1123, U24), Mercury230Dev);

	Mercury230FrequencyReg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1140, U24), Mercury230Dev);

	Mercury230PFReg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1130, S24), Mercury230Dev);
	Mercury230PF1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1131, S24), Mercury230Dev);
	Mercury230PF2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1132, S24), Mercury230Dev);
	Mercury230PF3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1133, S24), Mercury230Dev);

	Mercury230KU1Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1161, U16), Mercury230Dev);
	Mercury230KU2Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1162, U16), Mercury230Dev);
	Mercury230KU3Reg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1163, U16), Mercury230Dev);

	Mercury230TempReg = TVirtualRegister::Create(TRegisterConfig::Create(TMercury230Device::REG_PARAM_BE, 0x1170, U16), Mercury230Dev);


	SerialPort->Open();
}

TEST_F(TMercury230Test, ReadEnergy)
{
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
    ASSERT_EQ(3196200, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalConsumptionReg)));
    ASSERT_EQ(300444, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalReactiveEnergyReg)));
    ASSERT_EQ(3196200, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalConsumptionReg)));
    Mercury230Dev->EndPollCycle();

    EnqueueMercury230EnergyResponse2();
    ASSERT_EQ(3196201, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalConsumptionReg)));
    ASSERT_EQ(300445, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalReactiveEnergyReg)));
    ASSERT_EQ(3196201, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TotalConsumptionReg)));
    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

void TMercury230Test::VerifyParamQuery()
{
    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    ASSERT_EQ(24128, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230U1Reg)));

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(69, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230I1Reg)));

    EnqueueMercury230I2Response();
    // subparam 0x22 = current (phase 2)
    ASSERT_EQ(96, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230I2Reg)));

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230U2Reg)));

    EnqueueMercury230U3Response();
	// subparam 0x12 = voltage (phase 3)
	ASSERT_EQ(50405, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230U3Reg)));

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(553095, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230PReg)));

    EnqueueMercury230TempResponse();
    ASSERT_EQ(24, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230TempReg)));
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
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230NoSessionResponse();
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230U2Response();

    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230U2Reg)));

    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TMercury230Test, Exception)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        Mercury230Dev->ReadMemoryBlock(GetMemoryBlock(Mercury230U2Reg));
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
