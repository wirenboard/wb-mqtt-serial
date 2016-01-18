#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "serial_protocol.h"
#include "pty_based_fake_serial.h"

class TPtyBasedFakeSerialTest: public TLoggedFixture {
protected:
    void SetUp();
    void TearDown();

    void Write3(uint32_t v);
    // returns value via reference to make using FAIL() possible
    // inside the function
    void Read3(uint32_t &v);

    PPtyBasedFakeSerial FakeSerial;
    PAbstractSerialPort Serial;
};

void TPtyBasedFakeSerialTest::Write3(uint32_t v)
{
    uint8_t buf[3], *p = buf;
    for (int i = 16; i >= 0; i -= 8)
        *p++ = v >> i;
    Serial->WriteBytes(buf, 3);
}

void TPtyBasedFakeSerialTest::Read3(uint32_t &v)
{
    v = 0;
    for (int i = 0; i < 3; ++i)
        v = (v << 8) | Serial->ReadByte();
}

void TPtyBasedFakeSerialTest::SetUp()
{
    TLoggedFixture::SetUp();
    FakeSerial = PPtyBasedFakeSerial(new TPtyBasedFakeSerial(*this));
    Serial = PAbstractSerialPort(
        new TSerialPort(
            TSerialPortSettings(
                FakeSerial->GetPtsName(),
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

TEST_F(TPtyBasedFakeSerialTest, Check)
{
    FakeSerial->Expect({ 0x01, 0x02, 0x03 }, { 0x10, 0x20, 0x30 }, "foo");
    FakeSerial->Expect({ 0x11, 0x12, 0x13 }, { 0x21, 0x22, 0x23 }, "bar");

    Write3(0x010203);
    uint32_t v;
    Read3(v);
    ASSERT_EQ(0x102030, v);

    Write3(0x111213);
    Read3(v);
    ASSERT_EQ(0x212223, v);
}
