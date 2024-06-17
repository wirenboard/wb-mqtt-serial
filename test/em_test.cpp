#include "devices/mercury230_device.h"
#include "devices/milur_device.h"
#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include "milur_expectations.h"
#include <string>

class TEMDeviceTest: public TSerialDeviceTest, public TMilurExpectations, public TMercury230Expectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig() const;
    virtual PDeviceConfig Mercury230Config() const;
    PMilurDevice MilurDev;
    PMercury230Device Mercury230Dev;
    PRegister MilurPhaseCVoltageReg;
    PRegister MilurPhaseCCurrentReg;
    PRegister MilurTotalConsumptionReg;
    PRegister MilurFrequencyReg;
    PRegister Mercury230TotalReactiveEnergyReg;
    PRegister Mercury230TotalConsumptionReg;
    PRegister Mercury230U1Reg;
    PRegister Mercury230I1Reg;
    PRegister Mercury230U2Reg;
    PRegister Mercury230TempReg;
    PRegister Mercury230PReg;
};

PDeviceConfig TEMDeviceTest::MilurConfig() const
{
    return std::make_shared<TDeviceConfig>("milur", std::to_string(0xff), "milur");
}

PDeviceConfig TEMDeviceTest::Mercury230Config() const
{
    return std::make_shared<TDeviceConfig>("mercury230", std::to_string(0x00), "mercury230");
}

void TEMDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    MilurDev = std::make_shared<TMilurDevice>(MilurConfig(), SerialPort, DeviceFactory.GetProtocol("milur"));
    Mercury230Dev =
        std::make_shared<TMercury230Device>(Mercury230Config(), SerialPort, DeviceFactory.GetProtocol("mercury230"));

    MilurPhaseCVoltageReg = MilurDev->AddRegister(TRegisterConfig::Create(TMilurDevice::REG_PARAM, 102, U24));
    MilurPhaseCCurrentReg = MilurDev->AddRegister(TRegisterConfig::Create(TMilurDevice::REG_PARAM, 105, U24));
    MilurTotalConsumptionReg = MilurDev->AddRegister(TRegisterConfig::Create(TMilurDevice::REG_ENERGY, 118, BCD32));
    MilurFrequencyReg = MilurDev->AddRegister(TRegisterConfig::Create(TMilurDevice::REG_FREQ, 9, U16));
    Mercury230TotalConsumptionReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0000, U32));
    Mercury230TotalReactiveEnergyReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_VALUE_ARRAY, 0x0002, U32));
    Mercury230U1Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1111, U24));
    Mercury230I1Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1121, U24));
    Mercury230U2Reg = Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM, 0x1112, U24));
    Mercury230TempReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_BE, 0x1170, S16));
    Mercury230PReg =
        Mercury230Dev->AddRegister(TRegisterConfig::Create(TMercury230Device::REG_PARAM_SIGN_ACT, 0x1100, S24));

    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
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

void TEMDeviceTest::VerifyMercuryParamQuery()
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

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(TRegisterValue{24043}, Mercury230Dev->ReadRegisterImpl(Mercury230U2Reg));

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(TRegisterValue{553095}, Mercury230Dev->ReadRegisterImpl(Mercury230PReg));

    EnqueueMercury230TempResponse();
    ASSERT_EQ(TRegisterValue{24}, Mercury230Dev->ReadRegisterImpl(Mercury230TempReg));
}

TEST_F(TEMDeviceTest, Combined)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    MilurDev->InvalidateReadCache();

    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->InvalidateReadCache();

    for (int i = 0; i < 3; i++) {
        VerifyMilurQuery();
        MilurDev->InvalidateReadCache();

        VerifyMercuryParamQuery();
        Mercury230Dev->InvalidateReadCache();
    }

    SerialPort->Close();
}

class TEMCustomPasswordTest: public TEMDeviceTest
{
public:
    PDeviceConfig MilurConfig() const;

    PDeviceConfig Mercury230Config() const;
};

PDeviceConfig TEMCustomPasswordTest::MilurConfig() const
{
    PDeviceConfig device_config = TEMDeviceTest::MilurConfig();
    device_config->Password = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    device_config->AccessLevel = 2;
    return device_config;
}

PDeviceConfig TEMCustomPasswordTest::Mercury230Config() const
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
    MilurDev->InvalidateReadCache();

    EnqueueMercury230AccessLevel2SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Dev->InvalidateReadCache();

    SerialPort->Close();
}
