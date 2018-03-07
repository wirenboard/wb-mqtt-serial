#include <string>
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include "mercury230_expectations.h"
#include "milur_device.h"
#include "mercury230_device.h"
#include "protocol_register.h"

class TEMDeviceTest: public TSerialDeviceTest, public TMilurExpectations, public TMercury230Expectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();
    PMilurDevice MilurDev;
    PMercury230Device Mercury230Dev;
    PProtocolRegister MilurPhaseCVoltageReg;
    PProtocolRegister MilurPhaseCCurrentReg;
    PProtocolRegister MilurTotalConsumptionReg;
    PProtocolRegister MilurFrequencyReg;
    PProtocolRegister Mercury230TotalReactiveEnergyReg;
    PProtocolRegister Mercury230TotalConsumptionReg;
    PProtocolRegister Mercury230U1Reg;
    PProtocolRegister Mercury230I1Reg;
    PProtocolRegister Mercury230U2Reg;
    PProtocolRegister Mercury230TempReg;
    PProtocolRegister Mercury230PReg;
};

PDeviceConfig TEMDeviceTest::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", std::to_string(0xff), "milur");
}

PDeviceConfig TEMDeviceTest::Mercury230Config()
{
    return std::make_shared<TDeviceConfig>("mercury230", std::to_string(0x00), "mercury230");
}

void TEMDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort,
                            TSerialDeviceFactory::GetProtocol("milur"));
    Mercury230Dev = std::make_shared<TMercury230Device>(Mercury230Config(), SerialPort,
                            TSerialDeviceFactory::GetProtocol("mercury230"));


    MilurPhaseCVoltageReg = std::make_shared<TProtocolRegister>(102, TMilurDevice::REG_PARAM);
    MilurPhaseCVoltageReg = std::make_shared<TProtocolRegister>(102, TMilurDevice::REG_PARAM);
    MilurPhaseCCurrentReg = std::make_shared<TProtocolRegister>(105, TMilurDevice::REG_PARAM);
    MilurTotalConsumptionReg = std::make_shared<TProtocolRegister>(118, TMilurDevice::REG_ENERGY);
    MilurFrequencyReg = std::make_shared<TProtocolRegister>(9, TMilurDevice::REG_FREQ);
    Mercury230TotalConsumptionReg =
        std::make_shared<TProtocolRegister>(0x0000, TMercury230Device::REG_VALUE_ARRAY);
    Mercury230TotalReactiveEnergyReg =
        std::make_shared<TProtocolRegister>(0x0002, TMercury230Device::REG_VALUE_ARRAY);
    Mercury230U1Reg = std::make_shared<TProtocolRegister>(0x1111, TMercury230Device::REG_PARAM);
    Mercury230I1Reg = std::make_shared<TProtocolRegister>(0x1121, TMercury230Device::REG_PARAM);
    Mercury230U2Reg = std::make_shared<TProtocolRegister>(0x1112, TMercury230Device::REG_PARAM);
    Mercury230TempReg = std::make_shared<TProtocolRegister>(0x1170, TMercury230Device::REG_PARAM_BE);
    Mercury230PReg  = std::make_shared<TProtocolRegister>(0x1100, TMercury230Device::REG_PARAM_SIGN_ACT);

    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
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

void TEMDeviceTest::VerifyMercuryParamQuery()
{
    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    ASSERT_EQ(24128, Mercury230Dev->ReadProtocolRegister(Mercury230U1Reg));

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(69, Mercury230Dev->ReadProtocolRegister(Mercury230I1Reg));

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Dev->ReadProtocolRegister(Mercury230U2Reg));

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(553095, Mercury230Dev->ReadProtocolRegister(Mercury230PReg));

    EnqueueMercury230TempResponse();
    ASSERT_EQ(24, Mercury230Dev->ReadProtocolRegister(Mercury230TempReg));
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
