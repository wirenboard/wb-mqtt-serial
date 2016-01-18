#include "testlog.h"
#include "fake_serial_port.h"
#include "serial_connector.h"


class TIVTMProtocolTest: public TSerialProtocolDirectTest
{
protected:
    void SetUp();
};

void TIVTMProtocolTest::SetUp()
{
    TSerialProtocolDirectTest::SetUp();
    Context->AddDevice(std::make_shared<TDeviceConfig>("ivtm", 0x0001, "ivtm"));
    Context->AddDevice(std::make_shared<TDeviceConfig>("ivtm", 0x000A, "ivtm"));
}

TEST_F(TIVTMProtocolTest, IVTM7MQuery)
{
    Context->SetSlave(0x01);

    // >> 24 30 30 30 31 52 52 30 30 30 30 30 34 41 44 0d
    // << 21 30 30 30 31 52 52 43 45 44 33 44 31 34 31 35 46 0D 
    // temperature == 26.228420

    SerialPort->Expect(
        {
            '$', '0', '0', '0', '1', 'R', 'R', '0', '0', '0', '0', '0', '4', 'A', 'D', 0x0d
        },
        {
            // Session setup response
            '!',                  // header
            '0', '0', '0', '1',   // slave addr
            'R', 'R',             // read response
            'C', 'E', 'D', '3', 'D', '1', '4', '1', //temp data CE D3 D1 41 (little endian)
            '5', 'F',             //CRC
            0x0d                  // footer
        });

    uint64_t v;
    Context->ReadDirectRegister(0, &v, Float, 4);
    ASSERT_EQ(0x41D1D3CE, v); //big-endian


	// >> 24 30 30 30 31 52 52 30 30 30 34 30 34 42 31 0d
	// << 21 30 30 30 31 52 52 33 30 39 41 45 42 34 31 34 46 0D
    // humidity == 29.450287

    SerialPort->Expect(
        {
            '$', '0', '0', '0', '1', 'R', 'R', '0', '0', '0', '4', '0', '4', 'B', '1', 0x0d
        },
        {
            // Session setup response
            '!',                  // header
            '0', '0', '0', '1',   // slave addr
            'R', 'R',             // read response
            '3', '0', '9', 'A', 'E', 'B', '4', '1', //hum data 30 9A EB 41 (little endian)
            '4', 'F',             //CRC
            0x0D                  // footer
        });

    Context->ReadDirectRegister(4, &v, Float, 4);
    ASSERT_EQ(0x41EB9A30, v); //big-endian

    Context->EndPollCycle(0);
    Context->Disconnect();

    // Test upper-case hex letters

    Context->SetSlave(0x0A);

    // >> 24 30 30 30 41 52 52 30 30 30 30 30 34 42 44 0d
    // << 21 30 30 30 41 52 52 35 45 38 35 43 37 34 31 35 43 0D
    // temperature == 24.940121

    SerialPort->Expect(
        {
            '$', '0', '0', '0', 'A', 'R', 'R', '0', '0', '0', '0', '0', '4', 'B', 'D', 0x0d
        },
        {
            // Session setup response
            '!',                  // header
            '0', '0', '0', 'A',   // slave addr
            'R', 'R',             // read response
            '5', 'E', '8', '5', 'C', '7', '4', '1', //temp data 5E 85 C7 41 (little endian)
            '5', 'C',             //CRC
            0x0d                  // footer
        });

    Context->ReadDirectRegister(0, &v, Float, 4);
    ASSERT_EQ(0x41C7855E, v); //big-endian
}
