#include "fake_serial_port.h"
#include "em_expectations.h"
#include "milur_device.h"
#include "mercury230_device.h"
#include "mercury200_device.h"

class TEMDeviceTest: public TSerialDeviceTest, public TEMDeviceExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    void VerifyMercury200EnergyQuery();
    void VerifyMercury200ParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();
    virtual PDeviceConfig Mercury200Config();
    PMilurDevice MilurDev;
    PMercury230Device Mercury230Dev;
    PMercury200Device Mercury200Dev;
    PRegister MilurPhaseCVoltageReg;
    PRegister MilurTotalConsumptionReg;
    PRegister MilurFrequencyReg;
    PRegister Mercury230TotalReactiveEnergyReg;
    PRegister Mercury230TotalConsumptionReg;
    PRegister Mercury230U1Reg;
    PRegister Mercury230I1Reg;
    PRegister Mercury230U2Reg;
    PRegister Mercury230PReg;

    PRegister Mercury200RET1Reg;
    PRegister Mercury200RET2Reg;
    PRegister Mercury200RET3Reg;
    PRegister Mercury200RET4Reg;
    PRegister Mercury200UReg;
    PRegister Mercury200IReg;
    PRegister Mercury200PReg;
};

PDeviceConfig TEMDeviceTest::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", 0xff, "milur");
}

PDeviceConfig TEMDeviceTest::Mercury230Config()
{
    return std::make_shared<TDeviceConfig>("mercury230", 0x00, "mercury230");
}

PDeviceConfig TEMDeviceTest::Mercury200Config()
{
    return std::make_shared<TDeviceConfig>("mercury200", 123456, "mercury200");
}

void TEMDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("milur"));
    Mercury230Dev = std::make_shared<TMercury230Device>(Mercury230Config(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("mercury230"));
    Mercury200Dev = std::make_shared<TMercury200Device>(Mercury200Config(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("mercury200"));

    
    MilurPhaseCVoltageReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_PARAM, 102, U24));
    MilurTotalConsumptionReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));
    MilurFrequencyReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_FREQ, 9, U16));
    Mercury230TotalConsumptionReg =
        TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0000, U32));
    Mercury230TotalReactiveEnergyReg =
        TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0002, U32));
    Mercury230U1Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1111, U24));
    Mercury230I1Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1121, U24));
    Mercury230U2Reg = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1112, U24));
    Mercury230PReg  = TRegister::Intern(Mercury230Dev, TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1100, U24));
    
    Mercury200RET1Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2700, BCD32));
    Mercury200RET2Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2704, BCD32));
    Mercury200RET3Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2708, BCD32));
    Mercury200RET4Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x270C, BCD32));
    Mercury200UReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE16, 0x6300, BCD16));
    Mercury200IReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE16, 0x6302, BCD16));
    Mercury200PReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE24, 0x6304, BCD24));

    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurDev->ReadRegister(MilurPhaseCVoltageReg));

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    ASSERT_EQ(0x11144, MilurDev->ReadRegister(MilurTotalConsumptionReg));

    EnqueueMilurFrequencyResponse();
    ASSERT_EQ(50080, MilurDev->ReadRegister(MilurFrequencyReg));
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

void TEMDeviceTest::VerifyMercury200EnergyQuery()
{
    EnqueueMercury200EnergyResponse();
    ASSERT_EQ(0x62142, Mercury200Dev->ReadRegister(Mercury200RET1Reg));
    ASSERT_EQ(0x20834, Mercury200Dev->ReadRegister(Mercury200RET2Reg));
    ASSERT_EQ(0x11111, Mercury200Dev->ReadRegister(Mercury200RET3Reg));
    ASSERT_EQ(0x22222, Mercury200Dev->ReadRegister(Mercury200RET4Reg));
    Mercury200Dev->EndPollCycle();
}

void TEMDeviceTest::VerifyMercury200ParamQuery()
{
    EnqueueMercury200ParamResponse();
    ASSERT_EQ(0x1234, Mercury200Dev->ReadRegister(Mercury200UReg));
    ASSERT_EQ(0x5678, Mercury200Dev->ReadRegister(Mercury200IReg));
    ASSERT_EQ(0x765432, Mercury200Dev->ReadRegister(Mercury200PReg));
    Mercury200Dev->EndPollCycle();
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

TEST_F(TEMDeviceTest, Mercury200Energy)
{
    try {
        VerifyMercury200EnergyQuery();
    } catch(const TSerialDeviceException& e) {
        SerialPort->Close();
        throw e;
    } catch(const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TEMDeviceTest, Mercury200Params)
{
    try {
        VerifyMercury200ParamQuery();
    } catch(const TSerialDeviceException& e) {
        SerialPort->Close();
        throw e;
    } catch(const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TEMDeviceTest, Combined)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    MilurDev->EndPollCycle();

    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->EndPollCycle();

    for (int i = 0; i < 3; i++) {
        VerifyMilurQuery();
        MilurDev->EndPollCycle();

        VerifyMercuryParamQuery();
        Mercury230Dev->EndPollCycle();
    }

    SerialPort->Close();
}

class TEMCustomPasswordTest : public TEMDeviceTest {
public:
    PDeviceConfig MilurConfig();

    PDeviceConfig Mercury230Config();
};

PDeviceConfig TEMCustomPasswordTest::MilurConfig()
{
    PDeviceConfig device_config = TEMDeviceTest::MilurConfig();
    device_config->Password = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    device_config->AccessLevel = 2;
    return device_config;
}

PDeviceConfig TEMCustomPasswordTest::Mercury230Config()
{
    PDeviceConfig device_config = TEMDeviceTest::Mercury230Config();
    device_config->Password = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
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

class TMilur32Test: public TSerialDeviceTest, public TEMDeviceExpectations
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
    return std::make_shared<TDeviceConfig>("milur", 49932, "milur");
}

void TMilur32Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("milur"));
    MilurTotalConsumptionReg = TRegister::Intern(MilurDev, TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));
    
    SerialPort->Open();
}

void TMilur32Test::VerifyMilurQuery()
{
    EnqueueMilur32TotalConsumptionResponse();
    ASSERT_EQ(0x11144, MilurDev->ReadRegister(MilurTotalConsumptionReg));
}

TEST_F(TMilur32Test, MilurQuery)
{
    EnqueueMilur32SessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

//FIXME: ExpectNBytes() in Milur code isn't covered by tests here
