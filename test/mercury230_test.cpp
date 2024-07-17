#include "devices/mercury230_device.h"
#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include <string>

class TMercury230Test: public TSerialDeviceTest, public TMercury230Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig() const;

    PMercury230Device Mercury230Dev;

    PRegister Mercury230TotalReactiveEnergyReg;
    PRegister Mercury230TotalConsumptionReg;
    PRegister Mercury230PReg;
    PRegister Mercury230P1Reg;
    PRegister Mercury230P2Reg;
    PRegister Mercury230P3Reg;
    PRegister Mercury230QReg;
    PRegister Mercury230Q1Reg;
    PRegister Mercury230Q2Reg;
    PRegister Mercury230Q3Reg;
    PRegister Mercury230U1Reg;
    PRegister Mercury230U2Reg;
    PRegister Mercury230U3Reg;
    PRegister Mercury230I1Reg;
    PRegister Mercury230I2Reg;
    PRegister Mercury230I3Reg;
    PRegister Mercury230FrequencyReg;
    PRegister Mercury230PFReg;
    PRegister Mercury230PF1Reg;
    PRegister Mercury230PF2Reg;
    PRegister Mercury230PF3Reg;
    PRegister Mercury230KU1Reg;
    PRegister Mercury230KU2Reg;
    PRegister Mercury230KU3Reg;
    PRegister Mercury230TempReg;
};

PDeviceConfig TMercury230Test::GetDeviceConfig() const
{
    return std::make_shared<TDeviceConfig>("mercury230", std::to_string(0x00), "mercury230");
}

void TMercury230Test::SetUp()
{
    TSerialDeviceTest::SetUp();

    Mercury230Dev =
        std::make_shared<TMercury230Device>(GetDeviceConfig(), SerialPort, DeviceFactory.GetProtocol("mercury230"));
    Mercury230TotalConsumptionReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x00, U32));
    Mercury230TotalReactiveEnergyReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x00, U32));
    Mercury230TotalReactiveEnergyReg->SetDataOffset(2);

    Mercury230PReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1100, S24));
    Mercury230P1Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1101, S24));
    Mercury230P2Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1102, S24));
    Mercury230P3Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1103, S24));

    Mercury230QReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1104, S24));
    Mercury230Q1Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1105, S24));
    Mercury230Q2Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1106, S24));
    Mercury230Q3Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_REACT, 0x1107, S24));

    Mercury230U1Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1111, U24));
    Mercury230U2Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1112, U24));
    Mercury230U3Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1113, U24));

    Mercury230I1Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1121, U24));
    Mercury230I2Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1122, U24));
    Mercury230I3Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1123, U24));

    Mercury230FrequencyReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1140, U24));

    Mercury230PFReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1130, S24));
    Mercury230PF1Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1131, S24));
    Mercury230PF2Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1132, S24));
    Mercury230PF3Reg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1133, S24));

    Mercury230KU1Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1161, U16));
    Mercury230KU2Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1162, U16));
    Mercury230KU3Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1163, U16));

    Mercury230TempReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_BE, 0x1170, U16));

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
    ASSERT_EQ(TRegisterValue{3196200}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalConsumptionReg));
    ASSERT_EQ(TRegisterValue{300444}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(TRegisterValue{3196200}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalConsumptionReg));
    Mercury230Dev->InvalidateReadCache();

    EnqueueMercury230EnergyResponse2();
    ASSERT_EQ(TRegisterValue{3196201}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalConsumptionReg));
    ASSERT_EQ(TRegisterValue{300445}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(TRegisterValue{3196201}, Mercury230Dev->ReadRegisterImpl(Mercury230TotalConsumptionReg));
    Mercury230Dev->InvalidateReadCache();
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
    ASSERT_EQ(TRegisterValue{24128}, Mercury230Dev->ReadRegisterImpl(Mercury230U1Reg));

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(TRegisterValue{69}, Mercury230Dev->ReadRegisterImpl(Mercury230I1Reg));

    EnqueueMercury230I2Response();
    // subparam 0x22 = current (phase 2)
    ASSERT_EQ(TRegisterValue{96}, Mercury230Dev->ReadRegisterImpl(Mercury230I2Reg));

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(TRegisterValue{24043}, Mercury230Dev->ReadRegisterImpl(Mercury230U2Reg));

    EnqueueMercury230U3Response();
    // subparam 0x12 = voltage (phase 3)
    ASSERT_EQ(TRegisterValue{50405}, Mercury230Dev->ReadRegisterImpl(Mercury230U3Reg));

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(TRegisterValue{553095}, Mercury230Dev->ReadRegisterImpl(Mercury230PReg));

    EnqueueMercury230TempResponse();
    ASSERT_EQ(TRegisterValue{24}, Mercury230Dev->ReadRegisterImpl(Mercury230TempReg));
}

TEST_F(TMercury230Test, ReadParams)
{
    EnqueueMercury230SessionSetupResponse();
    VerifyParamQuery();
    Mercury230Dev->InvalidateReadCache();
    VerifyParamQuery();
    Mercury230Dev->InvalidateReadCache();
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
    ASSERT_EQ(TRegisterValue{24043}, Mercury230Dev->ReadRegisterImpl(Mercury230U2Reg));
    SerialPort->Close();
}

TEST_F(TMercury230Test, Exception)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        Mercury230Dev->ReadRegisterImpl(Mercury230U2Reg);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: Internal meter error", e.what());
        SerialPort->Close();
    }
}

class TMercury230CustomPasswordTest: public TMercury230Test
{
public:
    PDeviceConfig GetDeviceConfig() const override;
};

PDeviceConfig TMercury230CustomPasswordTest::GetDeviceConfig() const
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
    SerialPort->Close();
}

class TMercury230IntegrationTest: public TSerialDeviceIntegrationTest, public TMercury230Expectations
{
protected:
    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
    {
        return "configs/config-mercury230-test.json";
    }
    std::string GetTemplatePath() const override
    {
        return "device-templates";
    }
    void ExpectQueries(bool firstPoll);
};

void TMercury230IntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(7));
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

    EnqueueMercury230U1Response();
    EnqueueMercury230U2Response();
    EnqueueMercury230U3Response();

    EnqueueMercury230I1Response();
    EnqueueMercury230I2Response();
    EnqueueMercury230I3Response();

    EnqueueMercury230FrequencyResponse();

    EnqueueMercury230KU1Response();
    EnqueueMercury230KU2Response();
    EnqueueMercury230KU3Response();

    EnqueueMercury230PResponse();
    EnqueueMercury230P1Response();
    EnqueueMercury230P2Response();
    EnqueueMercury230P3Response();

    EnqueueMercury230PFResponse();
    EnqueueMercury230PF1Response();
    EnqueueMercury230PF2Response();
    EnqueueMercury230PF3Response();

    EnqueueMercury230QResponse();
    EnqueueMercury230Q1Response();
    EnqueueMercury230Q2Response();
    EnqueueMercury230Q3Response();

    EnqueueMercury230TempResponse();
    EnqueueMercury230PerPhaseEnergyResponse();
}

TEST_F(TMercury230IntegrationTest, Poll)
{
    ExpectQueries(true);
    Note() << "LoopOnce()";
    for (auto i = 0; i < 25; ++i) {
        SerialDriver->LoopOnce();
    }
}
