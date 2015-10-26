#include "testlog.h"
#include "fake_serial_port.h"
#include "../milur_protocol.h"

class TMilurProtocolTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    void EnqueueSessionSetupResponse();

    PFakeSerialPort SerialPort;
    PMilurProtocol MilurProtocol;
};

void TMilurProtocolTest::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
    MilurProtocol = PMilurProtocol(new TMilurProtocol(SerialPort));
}

void TMilurProtocolTest::TearDown()
{
    SerialPort.reset();
    MilurProtocol.reset();
    TLoggedFixture::TearDown();
}

void TMilurProtocolTest::EnqueueSessionSetupResponse()
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

TEST_F(TMilurProtocolTest, Query)
{
    MilurProtocol->Open();
    EnqueueSessionSetupResponse();
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
    ASSERT_EQ(0x03946f, MilurProtocol->ReadRegister(0xff, 102, U24));
    MilurProtocol->Close();
}

TEST_F(TMilurProtocolTest, Reconnect)
{
    MilurProtocol->Open();
    EnqueueSessionSetupResponse();
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
    EnqueueSessionSetupResponse();
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
    ASSERT_EQ(0x03946f, MilurProtocol->ReadRegister(0xff, 102, U24));
}
