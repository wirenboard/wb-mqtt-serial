#include "fake_serial_port.h"
#include "em_expectations.h"

class TEMProtocolTest: public TSerialProtocolDirectTest, public TEMProtocolExpectations
{
protected:
    void SetUp();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();
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
    TSerialProtocolDirectTest::SetUp();
    Context->AddDevice(MilurConfig());
    Context->AddDevice(Mercury230Config());
}

void TEMProtocolTest::VerifyMilurQuery()
{
    EnqueueMilurPhaseCVoltageResponse();
    uint64_t v;
    Context->ReadDirectRegister(102, &v, U24, 3);
    ASSERT_EQ(0x03946f, v);

    EnqueueMilurTotalConsumptionResponse();
    Context->ReadDirectRegister(118, &v, BCD32, 4);
    ASSERT_EQ(11144, v);
}

TEST_F(TEMProtocolTest, MilurQuery)
{
    Context->SetSlave(0xff);
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    Context->EndPollCycle(0);
    VerifyMilurQuery();
    Context->Disconnect();
}

TEST_F(TEMProtocolTest, MilurReconnect)
{
    Context->SetSlave(0xff);
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurNoSessionResponse();
    // reconnection
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    uint64_t v;
    Context->ReadDirectRegister(102, &v, U24, 3);
    ASSERT_EQ(0x03946f, v);
}

TEST_F(TEMProtocolTest, MilurException)
{
    Context->SetSlave(0xff);
    EnqueueMilurSessionSetupResponse();
    EnqueueMilurExceptionResponse();
    try {
        uint64_t v;
        Context->ReadDirectRegister(102, &v, U24, 3);
        FAIL() << "No exception thrown";
    } catch (const TModbusException& e) {
        ASSERT_STREQ("Modbus error: Serial protocol error: EEPROM access error", e.what());
        Context->EndPollCycle(0);
        Context->Disconnect();
    }
}

TEST_F(TEMProtocolTest, Mercury230ReadEnergy)
{
    Context->SetSlave(0x00);
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
    uint64_t v;
    Context->ReadDirectRegister(0x50000, &v, U32, 4);
    ASSERT_EQ(3196200, v);
    Context->ReadDirectRegister(0x50002, &v, U32, 4);
    ASSERT_EQ(300444, v);
    Context->ReadDirectRegister(0x50000, &v, U32, 4);
    ASSERT_EQ(3196200, v);
    Context->EndPollCycle(0);

    EnqueueMercury230EnergyResponse2();
    Context->ReadDirectRegister(0x50000, &v, U32, 4);
    ASSERT_EQ(3196201, v);
    Context->ReadDirectRegister(0x50002, &v, U32, 4);
    ASSERT_EQ(300445, v);
    Context->ReadDirectRegister(0x50000, &v, U32, 4);
    ASSERT_EQ(3196201, v);
    Context->EndPollCycle(0);
    Context->Disconnect();
}

void TEMProtocolTest::VerifyMercuryParamQuery()
{
    EnqueueMercury230U1Response();
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    uint64_t v;
    Context->ReadDirectRegister(0x81111, &v, U24, 3);
    ASSERT_EQ(24128, v);

    EnqueueMercury230I1Response();
    // subparam 0x21 = current (phase 1)
    Context->ReadDirectRegister(0x81121, &v, U24, 3);
    ASSERT_EQ(69, v);

    EnqueueMercury230U2Response();
    // subparam 0x12 = voltage (phase 2)
    Context->ReadDirectRegister(0x81112, &v, U24, 3);
    ASSERT_EQ(24043, v);
}

TEST_F(TEMProtocolTest, Mercury230ReadParams)
{
    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Context->EndPollCycle(0);
    VerifyMercuryParamQuery();
    Context->EndPollCycle(0);
    Context->Disconnect();
}

TEST_F(TEMProtocolTest, Mercury230Reconnect)
{
    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230NoSessionResponse();
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230U2Response();

    // subparam 0x12 = voltage (phase 2)
    uint64_t v;
    Context->ReadDirectRegister(0x81112, &v, U24, 3);
    ASSERT_EQ(24043, v);

    Context->EndPollCycle(0);
    Context->Disconnect();
}

TEST_F(TEMProtocolTest, Mercury230Exception)
{
    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230InternalMeterErrorResponse();
    try {
        uint64_t v;
        Context->ReadDirectRegister(0x81112, &v, U24, 3);
        FAIL() << "No exception thrown";
    } catch (const TModbusException& e) {
        ASSERT_STREQ("Modbus error: Serial protocol error: Internal meter error", e.what());
        Context->EndPollCycle(0);
        Context->Disconnect();
    }
}

TEST_F(TEMProtocolTest, Combined)
{
    Context->SetSlave(0xff);
    EnqueueMilurSessionSetupResponse();
    VerifyMilurQuery();
    Context->EndPollCycle(0);

    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    VerifyMercuryParamQuery();
    Context->EndPollCycle(0);

    for (int i = 0; i < 3; i ++) {
        Context->SetSlave(0xff);
        VerifyMilurQuery();
        Context->EndPollCycle(0);

        Context->SetSlave(0x00);
        VerifyMercuryParamQuery();
        Context->EndPollCycle(0);
    }
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
    Context->SetSlave(0xff);
    EnqueueMilurAccessLevel2SessionSetupResponse();
    VerifyMilurQuery();
    Context->EndPollCycle(0);

    Context->SetSlave(0x00);
    EnqueueMercury230AccessLevel2SessionSetupResponse();
    VerifyMercuryParamQuery();
    Context->EndPollCycle(0);
}
