#include "devices/milur_device.h"
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include <string>

class TMilurTest: public TSerialDeviceTest, public TMilurExpectations
{
protected:
    void SetUp();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig() const;

    PMilurDevice MilurDev;

    PRegister MilurPhaseAVoltageReg;
    PRegister MilurPhaseBVoltageReg;
    PRegister MilurPhaseCVoltageReg;

    PRegister MilurPhaseACurrentReg;
    PRegister MilurPhaseBCurrentReg;
    PRegister MilurPhaseCCurrentReg;

    PRegister MilurPhaseAActivePowerReg;
    PRegister MilurPhaseBActivePowerReg;
    PRegister MilurPhaseCActivePowerReg;
    PRegister MilurTotalActivePowerReg;

    PRegister MilurPhaseAReactivePowerReg;
    PRegister MilurPhaseBReactivePowerReg;
    PRegister MilurPhaseCReactivePowerReg;
    PRegister MilurTotalReactivePowerReg;

    PRegister MilurTotalConsumptionReg;
    PRegister MilurTotalReactiveEnergyReg;
    PRegister MilurFrequencyReg;
};

PDeviceConfig TMilurTest::GetDeviceConfig() const
{
    return std::make_shared<TDeviceConfig>("milur", std::to_string(0xff), "milur");
}

void TMilurTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    MilurDev = std::make_shared<TMilurDevice>(GetDeviceConfig(), SerialPort, DeviceFactory.GetProtocol("milur"));

    MilurPhaseAVoltageReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 100, U24));
    MilurPhaseBVoltageReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 101, U24));
    MilurPhaseCVoltageReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 102, U24));

    MilurPhaseACurrentReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 103, S24));
    MilurPhaseBCurrentReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 104, S24));
    MilurPhaseCCurrentReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 105, S24));

    MilurPhaseAActivePowerReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 106, S32));
    MilurPhaseBActivePowerReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 107, S32));
    MilurPhaseCActivePowerReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 108, S32));
    MilurTotalActivePowerReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 109, S32));

    MilurPhaseAReactivePowerReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 110, S32));
    MilurPhaseBReactivePowerReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 111, S32));
    MilurPhaseCReactivePowerReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 112, S32));
    MilurTotalReactivePowerReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_POWER, 113, S32));

    MilurTotalConsumptionReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));
    MilurTotalReactiveEnergyReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 127, BCD32));
    MilurFrequencyReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_FREQ, 9, U16));

    SerialPort->Open();
}

void TMilurTest::VerifyParamQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(TRegisterValue{0x03946f}, MilurDev->ReadRegisterImpl(MilurPhaseCVoltageReg));

    EnqueueMilurPhaseCCurrentResponse();
    ASSERT_EQ(TRegisterValue{0xffd8f0}, MilurDev->ReadRegisterImpl(MilurPhaseCCurrentReg));

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    ASSERT_EQ(TRegisterValue{0x11144}, MilurDev->ReadRegisterImpl(MilurTotalConsumptionReg));

    EnqueueMilurFrequencyResponse();
    ASSERT_EQ(TRegisterValue{50080}, MilurDev->ReadRegisterImpl(MilurFrequencyReg));
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
    ASSERT_EQ(TRegisterValue{0x03946f}, MilurDev->ReadRegisterImpl(MilurPhaseCVoltageReg));
}

TEST_F(TMilurTest, Exception)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        MilurDev->ReadRegisterImpl(MilurPhaseCVoltageReg);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: EEPROM access error", e.what());
        SerialPort->Close();
    }
}

class TMilurCustomPasswordTest: public TMilurTest
{
public:
    PDeviceConfig GetDeviceConfig() const override;
};

PDeviceConfig TMilurCustomPasswordTest::GetDeviceConfig() const
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
    SerialPort->Close();
}

class TMilurIntegrationTest: public TSerialDeviceIntegrationTest, public TMilurExpectations
{
protected:
    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
    {
        return "configs/config-milur-test.json";
    }
    std::string GetTemplatePath() const override
    {
        return "../templates";
    }
    void ExpectQueries(bool firstPoll);
};

void TMilurIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(5));
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
    for (auto i = 0; i < 17; ++i) {
        SerialDriver->LoopOnce();
    }
}

class TMilur32Test: public TSerialDeviceTest, public TMilurExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    virtual PDeviceConfig MilurConfig();
    PMilurDevice MilurDev;

    PRegister MilurTotalConsumptionReg;
};

PDeviceConfig TMilur32Test::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", std::to_string(49932), "milur");
}

void TMilur32Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort, DeviceFactory.GetProtocol("milur"));
    MilurTotalConsumptionReg =
        TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));

    SerialPort->Open();
}

void TMilur32Test::VerifyMilurQuery()
{
    EnqueueMilur32TotalConsumptionResponse();
    ASSERT_EQ(TRegisterValue{0x11144}, MilurDev->ReadRegisterImpl(MilurTotalConsumptionReg));
}

TEST_F(TMilur32Test, MilurQuery)
{
    EnqueueMilur32SessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

// FIXME: ExpectNBytes() in Milur code isn't covered by tests here
