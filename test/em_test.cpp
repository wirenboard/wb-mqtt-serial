#include "testlog.h"
#include "fake_serial_port.h"
#include "serial_connector.h"

class TEMProtocolTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    void EnqueueMilurSessionSetupResponse();
    void EnqueueMercury230SessionSetupResponse();
    void VerifyMilurQuery();
    void VerifyMercuryParamQuery();

    PFakeSerialPort SerialPort;
    PModbusContext Context;
};

void TEMProtocolTest::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
    Context = TSerialConnector().CreateContext(SerialPort);
    Context->AddDevice(0xff, "milur");
    Context->AddDevice(0x00, "mercury230");
}

void TEMProtocolTest::TearDown()
{
    SerialPort.reset();
    Context.reset();
    TLoggedFixture::TearDown();
}

void TEMProtocolTest::EnqueueMilurSessionSetupResponse()
{
    SerialPort->EnqueueResponse(
        {
            // Session setup response
            0xff, // unit id
            0x08, // op
            0x01, // result
            0x87, // crc
            0xf0  // crc
        });
}

void TEMProtocolTest::VerifyMilurQuery()
{
    SerialPort->EnqueueResponse(
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
        });
    uint64_t v;
    Context->ReadDirectRegister(102, &v, U24);
    ASSERT_EQ(0x03946f, v);
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
    // >> FF 01 66 C0 4A
    // << FF 81 08 00 67 D8
    SerialPort->EnqueueResponse(
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x08, // error code
            0x00, // service data
            0x67, // crc
            0xd8  // crc
        });
    // reconnection
    EnqueueMilurSessionSetupResponse();
    SerialPort->EnqueueResponse(
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
        });
    uint64_t v;
    Context->ReadDirectRegister(102, &v, U24);
    ASSERT_EQ(0x03946f, v);
}

TEST_F(TEMProtocolTest, MilurException)
{
    Context->SetSlave(0xff);
    EnqueueMilurSessionSetupResponse();
    SerialPort->EnqueueResponse(
        {
            // Session setup response
            0xff, // unit id
            0x81, // op + exception flag
            0x07, // error code
            0x00, // service data
            0x62, // crc
            0x28  // crc
        });
    try {
        uint64_t v;
        Context->ReadDirectRegister(102, &v, U24);
        FAIL() << "No exception thrown";
    } catch (const TModbusException& e) {
        ASSERT_STREQ("Modbus error: Serial protocol error: EEPROM access error", e.what());
        Context->EndPollCycle(0);
        Context->Disconnect();
    }
}


void TEMProtocolTest::EnqueueMercury230SessionSetupResponse()
{
    SerialPort->EnqueueResponse(
        {
            // Session setup response
            0x00, // unit id (group)
            0x00, // state
            0x01, // crc
            0xb0  // crc
        });
}

TEST_F(TEMProtocolTest, Mercury230ReadEnergy)
{
    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    SerialPort->EnqueueResponse(
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
        });

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
    Context->ReadDirectRegister(0x50000, &v, U32);
    ASSERT_EQ(3196200, v);
    Context->ReadDirectRegister(0x50002, &v, U32);
    ASSERT_EQ(300444, v);
    Context->ReadDirectRegister(0x50000, &v, U32);
    ASSERT_EQ(3196200, v);
    Context->EndPollCycle(0);

    SerialPort->EnqueueResponse(
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
        });

    Context->ReadDirectRegister(0x50000, &v, U32);
    ASSERT_EQ(3196201, v);
    Context->ReadDirectRegister(0x50002, &v, U32);
    ASSERT_EQ(300445, v);
    Context->ReadDirectRegister(0x50000, &v, U32);
    ASSERT_EQ(3196201, v);
    Context->EndPollCycle(0);
    Context->Disconnect();
}

void TEMProtocolTest::VerifyMercuryParamQuery()
{
    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x00, // U1
            0x40, // U1
            0x5e, // U1
            0xb0, // crc
            0x1c  // crc
        });
    // Register address for params:
    // 0000 0000 CCCC CCCC NNNN NNNN BBBB BBBB
    // C = command (0x08)
    // N = param number (0x11)
    // B = subparam spec (BWRI), 0x11 = voltage, phase 1
    uint64_t v;
    Context->ReadDirectRegister(0x81111, &v, U24);
    ASSERT_EQ(24128, v);

    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x00, // I1
            0x45, // I1
            0x00, // I1
            0x32, // crc
            0xb4  // crc
        });
    // subparam 0x21 = current (phase 1)
    Context->ReadDirectRegister(0x81121, &v, U24);
    ASSERT_EQ(69, v);

    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x00, // U2
            0xeb, // U2
            0x5d, // U2
            0x8f, // crc
            0x2d  // crc
        });
    // subparam 0x12 = voltage (phase 2)
    Context->ReadDirectRegister(0x81112, &v, U24);
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
    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x05, // error 5 = no session
            0xc1, // crc
            0xb3  // crc
        });
    // re-setup happens here
    EnqueueMercury230SessionSetupResponse();
    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x00, // U2
            0xeb, // U2
            0x5d, // U2
            0x8f, // crc
            0x2d  // crc
        });
    // subparam 0x12 = voltage (phase 2)
    uint64_t v;
    Context->ReadDirectRegister(0x81112, &v, U24);
    ASSERT_EQ(24043, v);

    Context->EndPollCycle(0);
    Context->Disconnect();
}

TEST_F(TEMProtocolTest, Mercury230Exception)
{
    Context->SetSlave(0x00);
    EnqueueMercury230SessionSetupResponse();
    SerialPort->EnqueueResponse(
        {
            0x00, // unit id (group)
            0x02, // error 2 = internal meter error
            0x80, // crc
            0x71  // crc
        });
    try {
        uint64_t v;
        Context->ReadDirectRegister(0x81112, &v, U24);
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
