#include "testlog.h"
#include "fake_serial_port.h"
#include "../mercury230_protocol.h"

class TMercury230ProtocolTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    void EnqueueSessionSetupResponse();

    PFakeSerialPort SerialPort;
    PMercury230Protocol Mercury230Protocol;
};

void TMercury230ProtocolTest::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
    Mercury230Protocol = PMercury230Protocol(new TMercury230Protocol(SerialPort));
}

void TMercury230ProtocolTest::TearDown()
{
    SerialPort.reset();
    Mercury230Protocol.reset();
    TLoggedFixture::TearDown();
}

void TMercury230ProtocolTest::EnqueueSessionSetupResponse()
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

TEST_F(TMercury230ProtocolTest, ReadEnergy)
{
    Mercury230Protocol->Open();
    EnqueueSessionSetupResponse();
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
    ASSERT_EQ(3196200, Mercury230Protocol->ReadRegister(0x00, 0x50000, U32));
    ASSERT_EQ(300444,  Mercury230Protocol->ReadRegister(0x00, 0x50002, U32));
    ASSERT_EQ(3196200, Mercury230Protocol->ReadRegister(0x00, 0x50000, U32));
    Mercury230Protocol->EndPollCycle();

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

    ASSERT_EQ(3196201, Mercury230Protocol->ReadRegister(0x00, 0x50000, U32));
    ASSERT_EQ(300445, Mercury230Protocol->ReadRegister(0x00, 0x50002, U32));
    ASSERT_EQ(3196201, Mercury230Protocol->ReadRegister(0x00, 0x50000, U32));
    Mercury230Protocol->EndPollCycle();

    Mercury230Protocol->Close();
}

TEST_F(TMercury230ProtocolTest, ReadParams)
{
    Mercury230Protocol->Open();
    EnqueueSessionSetupResponse();
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
    ASSERT_EQ(24128, Mercury230Protocol->ReadRegister(0x00, 0x81111, U24));

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
    ASSERT_EQ(69, Mercury230Protocol->ReadRegister(0x00, 0x81121, U24));

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
    ASSERT_EQ(24043, Mercury230Protocol->ReadRegister(0x00, 0x81112, U24));

    Mercury230Protocol->EndPollCycle();
    Mercury230Protocol->Close();
}

/*

voltage (phase 1):
>> 00 08 11 11 4d ba
<< 00 00 40 5E B0 1C
0x005e40 -> 24128

current (phase 1):
>> 00 08 11 21 4d ae
<< 00 00 45 00 32 B4
0x000045 -> 69

>> 00 08 11 12 0d bb
<< 00 05 C1 B3
-- no session setup response to voltage query

>> 00 01 01 01 01 01 01 01 01 77 81
<< 00 00 01 B0
-- re-setup session

>> 00 08 11 12 0d bb
<< 00 00 EB 5D 8F 2D
0x005deb -> 24043
-- proper response to voltage query

*/
