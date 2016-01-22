#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "serial_protocol.h"
#include "pty_based_fake_serial.h"

class TPtyBasedFakeSerialTest: public TLoggedFixture {
protected:
    void SetUp();
    void TearDown();

    void Write3(uint32_t v, PAbstractSerialPort port = PAbstractSerialPort());
    // returns value via reference to make using FAIL() possible
    // inside the function
    uint32_t Read3(PAbstractSerialPort port = PAbstractSerialPort());

    PPtyBasedFakeSerial FakeSerial;
    PAbstractSerialPort Serial;
};

void TPtyBasedFakeSerialTest::Write3(uint32_t v, PAbstractSerialPort port)
{
    if (!port)
        port = Serial;
    uint8_t buf[3], *p = buf;
    for (int i = 16; i >= 0; i -= 8)
        *p++ = v >> i;
    port->WriteBytes(buf, 3);
}

uint32_t TPtyBasedFakeSerialTest::Read3(PAbstractSerialPort port)
{
    if (!port)
        port = Serial;
    uint32_t v = 0;
    for (int i = 0; i < 3; ++i)
        v = (v << 8) | port->ReadByte();
    return v;
}

void TPtyBasedFakeSerialTest::SetUp()
{
    TLoggedFixture::SetUp();
    FakeSerial = PPtyBasedFakeSerial(new TPtyBasedFakeSerial(*this));
    Serial = PAbstractSerialPort(
        new TSerialPort(
            TSerialPortSettings(
                FakeSerial->GetPrimaryPtsName(),
                9600, 'N', 8, 1, 10000)));
    Serial->Open();
}

void TPtyBasedFakeSerialTest::TearDown()
{
    Serial->Close();
    Serial.reset();
    FakeSerial.reset();
    TLoggedFixture::TearDown();
}

TEST_F(TPtyBasedFakeSerialTest, Expect)
{
    FakeSerial->StartExpecting();
    FakeSerial->Expect({ 0x01, 0x02, 0x03 }, { 0x10, 0x20, 0x30 }, "foo");
    FakeSerial->Expect({ 0x11, 0x12, 0x13 }, { 0x21, 0x22, 0x23 }, "bar");

    Write3(0x010203);
    ASSERT_EQ(0x102030, Read3());

    Write3(0x111213);
    ASSERT_EQ(0x212223, Read3());
}

TEST_F(TPtyBasedFakeSerialTest, Forward)
{
    FakeSerial->StartForwarding();
    PAbstractSerialPort secondary_serial = PAbstractSerialPort(
        new TSerialPort(
            TSerialPortSettings(
                FakeSerial->GetSecondaryPtsName(),
                9600, 'N', 8, 1, 10000)));
    secondary_serial->Open();

    for (int i = 0; i < 3; ++i) {
        Write3(0x010203);
        ASSERT_EQ(0x010203, Read3(secondary_serial));
        Write3(0x102030, secondary_serial);
        ASSERT_EQ(0x102030, Read3());

        Write3(0x111213);
        ASSERT_EQ(0x111213, Read3(secondary_serial));
        Write3(0x212223, secondary_serial);
        ASSERT_EQ(0x212223, Read3());
        FakeSerial->Flush(); // shouldn't change anything here, but shouldn't hang either
    }
    secondary_serial->Close();
}
