#include <string>
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include "mercury230_expectations.h"
#include "milur_device.h"
#include "mercury230_device.h"


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
    PVirtualRegister MilurPhaseCVoltageReg;
    PVirtualRegister MilurPhaseCCurrentReg;
    PVirtualRegister MilurTotalConsumptionReg;
    PVirtualRegister MilurFrequencyReg;
    PVirtualRegister Mercury230TotalReactiveEnergyReg;
    PVirtualRegister Mercury230TotalConsumptionReg;
    PVirtualRegister Mercury230U1Reg;
    PVirtualRegister Mercury230I1Reg;
    PVirtualRegister Mercury230U2Reg;
    PVirtualRegister Mercury230TempReg;
    PVirtualRegister Mercury230PReg;
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

    MilurPhaseCVoltageReg = Reg(MilurDev, 102, TMilurDevice::REG_PARAM, U24);
    MilurPhaseCCurrentReg = Reg(MilurDev, 105, TMilurDevice::REG_PARAM, U24);
    MilurTotalConsumptionReg = Reg(MilurDev, 118, TMilurDevice::REG_ENERGY, BCD32);
    MilurFrequencyReg = Reg(MilurDev, 9, TMilurDevice::REG_FREQ, U16);
    Mercury230TotalConsumptionReg =
        Reg(Mercury230Dev, 0x0000, TMercury230Device::REG_VALUE_ARRAY, U32);
    Mercury230TotalReactiveEnergyReg =
        Reg(Mercury230Dev, 0x0002, TMercury230Device::REG_VALUE_ARRAY, U32);
    Mercury230U1Reg = Reg(Mercury230Dev, 0x1111, TMercury230Device::REG_PARAM, U24);
    Mercury230I1Reg = Reg(Mercury230Dev, 0x1121, TMercury230Device::REG_PARAM, U24);
    Mercury230U2Reg = Reg(Mercury230Dev, 0x1112, TMercury230Device::REG_PARAM, U24);
    Mercury230TempReg = Reg(Mercury230Dev, 0x1170, TMercury230Device::REG_PARAM_BE, S16);
    Mercury230PReg  = Reg(Mercury230Dev, 0x1100, TMercury230Device::REG_PARAM_SIGN_ACT, S24);

    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
{
    auto MilurPhaseCVoltageRegQuery     = GetReadQuery({ MilurPhaseCVoltageReg });
    auto MilurPhaseCCurrentRegQuery     = GetReadQuery({ MilurPhaseCCurrentReg });
    auto MilurTotalConsumptionRegQuery  = GetReadQuery({ MilurTotalConsumptionReg });
    auto MilurFrequencyRegQuery         = GetReadQuery({ MilurFrequencyReg });

    EnqueueMilurPhaseCVoltageResponse();
    TestRead(MilurPhaseCVoltageRegQuery);
    ASSERT_EQ(0x03946f, MilurPhaseCVoltageReg->GetValue());

    EnqueueMilurPhaseCCurrentResponse();
    TestRead(MilurPhaseCCurrentRegQuery);
    ASSERT_EQ(0xffd8f0, MilurPhaseCCurrentReg->GetValue());

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    TestRead(MilurTotalConsumptionRegQuery);
    ASSERT_EQ(0x11144, MilurTotalConsumptionReg->GetValue());

    EnqueueMilurFrequencyResponse();
    TestRead(MilurFrequencyRegQuery);
    ASSERT_EQ(50080, MilurFrequencyReg->GetValue());
}

void TEMDeviceTest::VerifyMercuryParamQuery()
{
    auto Mercury230U1RegQuery      = GetReadQuery({ Mercury230U1Reg });
    auto Mercury230I1RegQuery      = GetReadQuery({ Mercury230I1Reg });
    auto Mercury230U2RegQuery      = GetReadQuery({ Mercury230U2Reg });
    auto Mercury230PRegQuery       = GetReadQuery({ Mercury230PReg });
    auto Mercury230TempRegQuery    = GetReadQuery({ Mercury230TempReg });

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

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    TestRead(Mercury230U2RegQuery);
    ASSERT_EQ(24043, Mercury230U2Reg->GetValue());

    EnqueueMercury230PResponse();
    // Total power (P)
    TestRead(Mercury230PRegQuery);
    ASSERT_EQ(553095, Mercury230PReg->GetValue());

    EnqueueMercury230TempResponse();
    TestRead(Mercury230TempRegQuery);
    ASSERT_EQ(24, Mercury230TempReg->GetValue());
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
