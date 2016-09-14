#include "fake_serial_port.h"
#include "em_expectations.h"
#include "milur_device.h"
#include "mercury230_device.h"

class TEMDeviceTest: public TSerialDeviceTest, public TEMDeviceExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();
    PMilurDevice MilurDev;
    PMercury230Device Mercury230Dev;
    
    PRegister MilurPhaseCVoltageReg;
    PRegister MilurTotalConsumptionReg;
    PRegister Mercury230TotalConsumptionReg;
    PRegister Mercury230TotalReactiveEnergyReg;
    PRegister Mercury230U1Reg;
    PRegister Mercury230I1Reg;
    PRegister Mercury230U2Reg;
    PRegister Mercury230PReg;
};

PDeviceConfig TEMDeviceTest::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", 0xff, "milur");
}

PDeviceConfig TEMDeviceTest::Mercury230Config()
{
    return std::make_shared<TDeviceConfig>("mercury230", 0x00, "mercury230");
}

void TEMDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("milur"));
    Mercury230Dev = std::make_shared<TMercury230Device>(Mercury230Config(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("mercury230"));
    
    MilurPhaseCVoltageReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 102, U24));
    MilurTotalConsumptionReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));
    Mercury230TotalConsumptionReg =
        TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0000, U32));
    Mercury230TotalReactiveEnergyReg =
        TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0002, U32));
    Mercury230U1Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1111, U24));
    Mercury230I1Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1121, U24));
    Mercury230U2Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1112, U24));
    Mercury230PReg  = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1100, U24));
    
    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurDev->ReadRegister(MilurPhaseCVoltageReg));

    EnqueueMilurTotalConsumptionResponse();
    ASSERT_EQ(11144, MilurDev->ReadRegister(MilurTotalConsumptionReg));
}

TEST_F(TEMDeviceTest, MilurQuery)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

TEST_F(TEMDeviceTest, MilurReconnect)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurNoSessionResponse();
    // reconnection
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurDev->ReadRegister(MilurPhaseCVoltageReg));
}

TEST_F(TEMDeviceTest, MilurException)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        MilurDev->ReadRegister(MilurPhaseCVoltageReg);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: EEPROM access error", e.what());
        SerialPort->Close();
    }
}

TEST_F(TEMDeviceTest, Mercury230ReadEnergy)
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
    ASSERT_EQ(3196200, Mercury230Dev->ReadRegister(Mercury230TotalConsumptionReg));
    ASSERT_EQ(300444, Mercury230Dev->ReadRegister(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(3196200, Mercury230Dev->ReadRegister(Mercury230TotalConsumptionReg));
    Mercury230Dev->EndPollCycle();

    EnqueueMercury230EnergyResponse2();
    ASSERT_EQ(3196201, Mercury230Dev->ReadRegister(Mercury230TotalConsumptionReg));
    ASSERT_EQ(300445, Mercury230Dev->ReadRegister(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(3196201, Mercury230Dev->ReadRegister(Mercury230TotalConsumptionReg));
    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

void TEMDeviceTest::VerifyMercuryParamQuery()
{
    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    ASSERT_EQ(24128, Mercury230Dev->ReadRegister(Mercury230U1Reg));

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(69, Mercury230Dev->ReadRegister(Mercury230I1Reg));

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Dev->ReadRegister(Mercury230U2Reg));

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(553095, Mercury230Dev->ReadRegister(Mercury230PReg));
}

TEST_F(TEMDeviceTest, Mercury230ReadParams)
{
    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->EndPollCycle();
    VerifyMercuryParamQuery();
    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TEMDeviceTest, Mercury230Reconnect)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230NoSessionResponse();
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230U2Response();

    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Dev->ReadRegister(Mercury230U2Reg));

    Mercury230Dev->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TEMDeviceTest, Mercury230Exception)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        Mercury230Dev->ReadRegister(Mercury230U2Reg);
        FAIL() << "No exception thrown";
    } catch (const TSerialDeviceException& e) {
        ASSERT_STREQ("Serial protocol error: Internal meter error", e.what());
        SerialPort->Close();
    }
}

TEST_F(TEMDeviceTest, Combined)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    MilurDev->EndPollCycle();

    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->EndPollCycle();

    for (int i = 0; i < 3; i ++) {
        VerifyMilurQuery();
        MilurDev->EndPollCycle();

        VerifyMercuryParamQuery();
        Mercury230Dev->EndPollCycle();
    }

    SerialPort->Close();
}

class TEMCustomPasswordTest: public TEMDeviceTest {
public:
    PDeviceConfig MilurConfig();
    PDeviceConfig Mercury230Config();
};

PDeviceConfig TEMCustomPasswordTest::MilurConfig()
{
    PDeviceConfig device_config = TEMDeviceTest::MilurConfig();
    device_config->Password = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    device_config->AccessLevel = 2;
    return device_config;
}

PDeviceConfig TEMCustomPasswordTest::Mercury230Config()
{
    PDeviceConfig device_config = TEMDeviceTest::Mercury230Config();
    device_config->Password = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    device_config->AccessLevel = 2;
    return device_config;
}

TEST_F(TEMCustomPasswordTest, Combined)
{
    EnqueueMilurAccessLevel2SessionSetupResponse();
    VerifyMilurQuery();
    MilurDev->EndPollCycle();

    EnqueueMercury230AccessLevel2SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->EndPollCycle();

    SerialPort->Close();
}
