#include <string>
#include "fake_serial_port.h"
#include "milur_expectations.h"
#include "mercury230_expectations.h"
#include "milur_device.h"
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
    PMemoryBlock MilurPhaseCVoltageReg;
    PMemoryBlock MilurPhaseCCurrentReg;
    PMemoryBlock MilurTotalConsumptionReg;
    PMemoryBlock MilurFrequencyReg;
    PMemoryBlock Mercury230ValArrayMB;
    PMemoryBlock Mercury230U1Reg;
    PMemoryBlock Mercury230I1Reg;
    PMemoryBlock Mercury230U2Reg;
    PMemoryBlock Mercury230TempReg;
    PMemoryBlock Mercury230PReg;
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

    MilurPhaseCVoltageReg = MilurDev->GetCreateMemoryBlock(102, TMilurDevice::REG_PARAM);
    MilurPhaseCCurrentReg = MilurDev->GetCreateMemoryBlock(105, TMilurDevice::REG_PARAM);
    MilurTotalConsumptionReg = MilurDev->GetCreateMemoryBlock(118, TMilurDevice::REG_ENERGY);
    MilurFrequencyReg = MilurDev->GetCreateMemoryBlock(9, TMilurDevice::REG_FREQ);
    Mercury230ValArrayMB = Mercury230Dev->GetCreateMemoryBlock(0, TMercury230Device::REG_VALUE_ARRAY);
    Mercury230U1Reg = Mercury230Dev->GetCreateMemoryBlock(0x1111, TMercury230Device::REG_PARAM, 3);
    Mercury230I1Reg = Mercury230Dev->GetCreateMemoryBlock(0x1121, TMercury230Device::REG_PARAM, 3);
    Mercury230U2Reg = Mercury230Dev->GetCreateMemoryBlock(0x1112, TMercury230Device::REG_PARAM, 3);
    Mercury230TempReg = Mercury230Dev->GetCreateMemoryBlock(0x1170, TMercury230Device::REG_PARAM_BE, 2);
    Mercury230PReg  = Mercury230Dev->GetCreateMemoryBlock(0x1100, TMercury230Device::REG_PARAM_SIGN_ACT);

    SerialPort->Open();
}

void TEMDeviceTest::VerifyMilurQuery()
{
    auto MilurPhaseCVoltageRegQuery     = GetReadQuery({ MilurPhaseCVoltageReg });
    auto MilurPhaseCCurrentRegQuery     = GetReadQuery({ MilurPhaseCCurrentReg });
    auto MilurTotalConsumptionRegQuery  = GetReadQuery({ MilurTotalConsumptionReg });
    auto MilurFrequencyRegQuery         = GetReadQuery({ MilurFrequencyReg });

    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, TestRead(MilurPhaseCVoltageRegQuery)[0]);

    EnqueueMilurPhaseCCurrentResponse();
    ASSERT_EQ(0xffd8f0, TestRead(MilurPhaseCCurrentRegQuery)[0]);

    EnqueueMilurTotalConsumptionResponse();
    // "milur BCD32" value 11144 packed as uint64_t
    ASSERT_EQ(0x11144, TestRead(MilurTotalConsumptionRegQuery)[0]);

    EnqueueMilurFrequencyResponse();
    ASSERT_EQ(50080, TestRead(MilurFrequencyRegQuery)[0]);
}

void TEMDeviceTest::VerifyMercuryParamQuery()
{
    auto Mercury230ValArrayMBQuery = GetReadQuery({ Mercury230ValArrayMB });
    auto Mercury230U1RegQuery      = GetReadQuery({ Mercury230U1Reg });
    auto Mercury230I1RegQuery      = GetReadQuery({ Mercury230I1Reg });
    auto Mercury230U2RegQuery      = GetReadQuery({ Mercury230U2Reg });
    auto Mercury230PRegQuery       = GetReadQuery({ Mercury230PReg });
    auto Mercury230TempRegQuery    = GetReadQuery({ Mercury230TempReg });

    EnqueueMercury230EnergyResponse1();
    const auto values = TestRead(Mercury230ValArrayMBQuery);
    ASSERT_EQ(4, values.size());
    ASSERT_EQ(3196200, values[0]);
    ASSERT_EQ(300444,  values[2]);

    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    ASSERT_EQ(24128, TestRead(Mercury230U1RegQuery)[0]);

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(69, TestRead(Mercury230I1RegQuery)[0]);

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, TestRead(Mercury230U2RegQuery)[0]);

    EnqueueMercury230PResponse();
    // Total power (P)
    ASSERT_EQ(553095, TestRead(Mercury230PRegQuery)[0]);

    EnqueueMercury230TempResponse();
    ASSERT_EQ(24, TestRead(Mercury230TempRegQuery)[0]);
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
