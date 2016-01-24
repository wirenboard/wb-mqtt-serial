#include "fake_serial_port.h"
#include "em_expectations.h"
#include "milur_protocol.h"
#include "mercury230_protocol.h"

namespace {
    PRegister MilurPhaseCVoltageReg(new TRegister(0xff, TMilurProtocol::REG_PARAM, 102, U24));
    PRegister MilurTotalConsumptionReg(new TRegister(0xff, TMilurProtocol::REG_ENERGY, 118, BCD32));
    PRegister Mercury230TotalConsumptionReg(
        new TRegister(0x00, TMercury230Protocol::REG_VALUE_ARRAY, 0x0000, U32));
    PRegister Mercury230TotalReactiveEnergyReg(
        new TRegister(0x00, TMercury230Protocol::REG_VALUE_ARRAY, 0x0002, U32));
    PRegister Mercury230U1Reg(new TRegister(0x00, TMercury230Protocol::REG_PARAM, 0x1111, U24));
    PRegister Mercury230I1Reg(new TRegister(0x00, TMercury230Protocol::REG_PARAM, 0x1121, U24));
    PRegister Mercury230U2Reg(new TRegister(0x00, TMercury230Protocol::REG_PARAM, 0x1112, U24));
};

class TEMProtocolTest: public TSerialProtocolTest, public TEMProtocolExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();
    PMilurProtocol MilurProto;
    PMercury230Protocol Mercury230Proto;
};

PDeviceConfig TEMProtocolTest::MilurConfig()
{
    return std::make_shared<TDeviceConfig>("milur", 0xff, "milur");
}

PDeviceConfig TEMProtocolTest::Mercury230Config()
{
    return std::make_shared<TDeviceConfig>("mercury230", 0x00, "mercury230");
}

void TEMProtocolTest::SetUp()
{
    TSerialProtocolTest::SetUp();
    MilurProto = std::make_shared<TMilurProtocol>(MilurConfig(), SerialPort);
    Mercury230Proto = std::make_shared<TMercury230Protocol>(Mercury230Config(), SerialPort);
    SerialPort->Open();
}

void TEMProtocolTest::VerifyMilurQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurProto->ReadRegister(MilurPhaseCVoltageReg));

    EnqueueMilurTotalConsumptionResponse();
    ASSERT_EQ(11144, MilurProto->ReadRegister(MilurTotalConsumptionReg));
}

TEST_F(TEMProtocolTest, MilurQuery)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    VerifyMilurQuery();
    SerialPort->Close();
}

TEST_F(TEMProtocolTest, MilurReconnect)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurNoSessionResponse();
    // reconnection
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    ASSERT_EQ(0x03946f, MilurProto->ReadRegister(MilurPhaseCVoltageReg));
}

TEST_F(TEMProtocolTest, MilurException)
{
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        MilurProto->ReadRegister(MilurPhaseCVoltageReg);
        FAIL() << "No exception thrown";
    } catch (const TSerialProtocolException& e) {
        ASSERT_STREQ("Serial protocol error: EEPROM access error", e.what());
        SerialPort->Close();
    }
}

TEST_F(TEMProtocolTest, Mercury230ReadEnergy)
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
    ASSERT_EQ(3196200, Mercury230Proto->ReadRegister(Mercury230TotalConsumptionReg));
    ASSERT_EQ(300444, Mercury230Proto->ReadRegister(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(3196200, Mercury230Proto->ReadRegister(Mercury230TotalConsumptionReg));
    Mercury230Proto->EndPollCycle();

    EnqueueMercury230EnergyResponse2();
    ASSERT_EQ(3196201, Mercury230Proto->ReadRegister(Mercury230TotalConsumptionReg));
    ASSERT_EQ(300445, Mercury230Proto->ReadRegister(Mercury230TotalReactiveEnergyReg));
    ASSERT_EQ(3196201, Mercury230Proto->ReadRegister(Mercury230TotalConsumptionReg));
    Mercury230Proto->EndPollCycle();
    SerialPort->Close();
}

void TEMProtocolTest::VerifyMercuryParamQuery()
{
    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    ASSERT_EQ(24128, Mercury230Proto->ReadRegister(Mercury230U1Reg));

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    ASSERT_EQ(69, Mercury230Proto->ReadRegister(Mercury230I1Reg));

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Proto->ReadRegister(Mercury230U2Reg));
}

TEST_F(TEMProtocolTest, Mercury230ReadParams)
{
    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Proto->EndPollCycle();
    VerifyMercuryParamQuery();
    Mercury230Proto->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TEMProtocolTest, Mercury230Reconnect)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230NoSessionResponse();
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230U2Response();

    // subparam 0x12 = voltage (phase 2)
    ASSERT_EQ(24043, Mercury230Proto->ReadRegister(Mercury230U2Reg));

    Mercury230Proto->EndPollCycle();
    SerialPort->Close();
}

TEST_F(TEMProtocolTest, Mercury230Exception)
{
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        Mercury230Proto->ReadRegister(Mercury230U2Reg);
        FAIL() << "No exception thrown";
    } catch (const TSerialProtocolException& e) {
        ASSERT_STREQ("Serial protocol error: Internal meter error", e.what());
        SerialPort->Close();
    }
}

TEST_F(TEMProtocolTest, Combined)
{
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    MilurProto->EndPollCycle();

    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Proto->EndPollCycle();

    for (int i = 0; i < 3; i ++) {
        VerifyMilurQuery();
        MilurProto->EndPollCycle();

        VerifyMercuryParamQuery();
        Mercury230Proto->EndPollCycle();
    }

    SerialPort->Close();
}

class TEMCustomPasswordTest: public TEMProtocolTest {
public:
    PDeviceConfig MilurConfig();
    PDeviceConfig Mercury230Config();
};

PDeviceConfig TEMCustomPasswordTest::MilurConfig()
{
    PDeviceConfig device_config = TEMProtocolTest::MilurConfig();
    device_config->Password = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    device_config->AccessLevel = 2;
    return device_config;
}

PDeviceConfig TEMCustomPasswordTest::Mercury230Config()
{
    PDeviceConfig device_config = TEMProtocolTest::Mercury230Config();
    device_config->Password = { 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    device_config->AccessLevel = 2;
    return device_config;
}

TEST_F(TEMCustomPasswordTest, Combined)
{
    EnqueueMilurAccessLevel2SessionSetupResponse();
    VerifyMilurQuery();
    MilurProto->EndPollCycle();

    EnqueueMercury230AccessLevel2SessionSetupResponse();
    VerifyMercuryParamQuery();
    Mercury230Proto->EndPollCycle();

    SerialPort->Close();
}
