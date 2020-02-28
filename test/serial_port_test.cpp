#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "serial_device.h"
#include "serial_port.h"
#include "serial_port_settings.h"
#include "pty_based_fake_serial.h"

class TSerialPortTest: public TLoggedFixture {
protected:
    void SetUp();
    void TearDown();

    PPtyBasedFakeSerial FakeSerial;
    PPort Serial;
    PPort SecondarySerial;
};

void TSerialPortTest::SetUp()
{
    TLoggedFixture::SetUp();
    FakeSerial = PPtyBasedFakeSerial(new TPtyBasedFakeSerial(*this));
    auto settings = std::make_shared<TSerialPortSettings>(
                FakeSerial->GetPrimaryPtsName(),
                9600, 'N', 8, 1, std::chrono::milliseconds(10000));
    Serial = PPort(
        new TSerialPort(settings));
    Serial->SetDebug(true);
    Serial->Open();

    FakeSerial->StartForwarding();
    auto secondary_settings = std::make_shared<TSerialPortSettings>(
                FakeSerial->GetSecondaryPtsName(),
                9600, 'N', 8, 1, std::chrono::milliseconds(10000));
    SecondarySerial = PPort(
        new TSerialPort(
            secondary_settings));
    SecondarySerial->Open();


}

void TSerialPortTest::TearDown()
{
    Serial->Close();
    Serial.reset();
    FakeSerial.reset();
    SecondarySerial->Close();
    TLoggedFixture::TearDown();
}


TEST_F(TSerialPortTest, TestSkipNoise)
{
    uint8_t buf[] = {1,2,3};
    Serial->WriteBytes(buf, sizeof(buf));
    SecondarySerial->SetDebug(true);
    SecondarySerial->SkipNoise();

    buf[0] = 0x04;
    // Should read 0x04, not 0x01
    Serial->WriteBytes(buf, 1);
    uint8_t read_back = SecondarySerial->ReadByte();
    ASSERT_EQ(read_back, buf[0]);

    FakeSerial->Flush(); // shouldn't change anything here, but shouldn't hang either
}