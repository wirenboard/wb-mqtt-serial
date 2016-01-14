#include "em_test.h"

void TEMProtocolTestBase::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
}

void TEMProtocolTestBase::TearDown()
{
    SerialPort.reset();
    TLoggedFixture::TearDown();
}

void TEMProtocolTestBase::EnqueueMilurSessionSetupResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x08, // op
            0x01, // access level
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0xff, // pw
            0x5f, // crc
            0xed  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x08, // op
            0x01, // result
            0x87, // crc
            0xf0  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMilurAccessLevel2SessionSetupResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x08, // op
            0x02, // access level
            0x02, // pw
            0x03, // pw
            0x04, // pw
            0x05, // pw
            0x06, // pw
            0x07, // pw
            0x7b, // crc
            0xd3  // crc
        },
        {
            // Session setup response
            // Different access level, thus not using EnqueueMilurSessionSetupResponse() here
            0xff, // unit id
            0x08, // op
            0x02, // result
            0xc7, // crc
            0xf1  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMilurPhaseCVoltageResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            0x66, // register
            0x03, // len
            0x6f, // data 1
            0x94, // data 2
            0x03, // data 3
            0x03, // crc
            0x4e  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMilurTotalConsumptionResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x76, // register
            0xc1, // crc
            0x86  // crc
        },
        {
            // Read response
            0xff, // unit id
            0x01, // op
            0x76, // register
            0x04, // len
            0x44, // data 1
            0x11, // data 2
            0x10, // data 3
            0x00, // data 4
            0xac, // crc
            0x6c  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMilurNoSessionResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x08, // error code
            0x00, // service data
            0x67, // crc
            0xd8  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMilurExceptionResponse()
{
    SerialPort->Expect(
        {
            0xff, // unit id
            0x01, // op
            0x66, // register
            0xc0, // crc
            0x4a  // crc
        },
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x07, // error code
            0x00, // service data
            0x62, // crc
            0x28  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230SessionSetupResponse()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x01, // op
            0x01, // access level
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x01, // pw
            0x77, // crc
            0x81  // crc
        },
        {
            // Session setup response
            0x00, // unit id (group)
            0x00, // state
            0x01, // crc
            0xb0  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230AccessLevel2SessionSetupResponse()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x01, // op
            0x02, // access level
            0x12, // pw
            0x13, // pw
            0x14, // pw
            0x15, // pw
            0x16, // pw
            0x17, // pw
            0x34, // crc
            0x17  // crc
        },
        {
            // Session setup response
            0x00, // unit id (group)
            0x00, // state
            0x01, // crc
            0xb0  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230EnergyResponse1()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x05, // op
            0x00, // addr
            0x00, // addr
            0x10, // crc
            0x25  // crc
        },
        {
            // Read response
            0x00, // unit id (group)
            0x30, // A+
            0x00, // A+
            0x28, // A+
            0xc5, // A+
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0x04, // R+
            0x00, // R+
            0x9c, // R+
            0x95, // R+
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0x44, // crc
            0xab  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230EnergyResponse2()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x05, // op
            0x00, // addr
            0x00, // addr
            0x10, // crc
            0x25  // crc
        },
        {
            // Read response
            0x00, // unit id (group)
            0x30, // A+
            0x00, // A+
            0x29, // A+
            0xc5, // A+
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0xff, // A-
            0x04, // R+
            0x00, // R+
            0x9d, // R+
            0x95, // R+
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0xff, // R-
            0x45, // crc
            0xbb  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230U1Response()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x11, // addr
            0x4d, // crc 
            0xba  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // U1
            0x40, // U1
            0x5e, // U1
            0xb0, // crc
            0x1c  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230I1Response()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x21, // addr
            0x4d, // crc
            0xae  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // I1
            0x45, // I1
            0x00, // I1
            0x32, // crc
            0xb4  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230U2Response()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x00, // U2
            0xeb, // U2
            0x5d, // U2
            0x8f, // crc
            0x2d  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230NoSessionResponse()
{
    SerialPort->Expect(
        {
            0x00, // unit id (group)
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x05, // error 5 = no session
            0xc1, // crc
            0xb3  // crc
        }, __func__);
}

void TEMProtocolTestBase::EnqueueMercury230InternalMeterErrorResponse()
{
    SerialPort->Expect(
        {
            0x00, // unit id
            0x08, // op
            0x11, // addr
            0x12, // addr
            0x0d, // crc
            0xbb  // crc
        },
        {
            0x00, // unit id (group)
            0x02, // error 2 = internal meter error
            0x80, // crc
            0x71  // crc
        }, __func__);
}

class TEMProtocolTest: public TEMProtocolTestBase
{
protected:
    void SetUp();
    void TearDown();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();
    virtual PDeviceConfig MilurConfig();
    virtual PDeviceConfig Mercury230Config();

    PModbusContext Context;
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
    TEMProtocolTestBase::SetUp();
    Context = TSerialConnector().CreateContext(SerialPort);
    Context->AddDevice(MilurConfig());
    Context->AddDevice(Mercury230Config());
}

void TEMProtocolTest::TearDown()
{
    Context.reset();
    TEMProtocolTestBase::TearDown();
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
