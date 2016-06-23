#include "testlog.h"
#include "fake_serial_port.h"
#include "pulsar_device.h"

namespace {
    PSlaveEntry HeatMeter = TSlaveEntry::Intern("pulsar", 107080);

    PRegister Heat_TempIn = TRegister::Intern(HeatMeter, 0, 2, Float);
    PRegister Heat_TempOut = TRegister::Intern(HeatMeter, 0, 3, Float);
    // TODO: time register
};

class TPulsarDeviceTest: public TSerialDeviceTest
{
protected:
    void SetUp();
    PPulsarDevice Dev;
};

void TPulsarDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();

    // Create device with fixed Slave ID
    Dev = std::make_shared<TPulsarDevice>(
        std::make_shared<TDeviceConfig>("pulsar-heat", 102030, "pulsar"),
        SerialPort);
    SerialPort->Open();
}

TEST_F(TPulsarDeviceTest, PulsarHeatMeterFloatQuery)
{
    // >> 00 10 70 80 01 0e 04 00 00 00 00 00 7C A7
    // << 00 10 70 80 01 0E 5A B3 C5 41 00 00 18 DB
    // temperature == 24.71257

    SerialPort->Expect(
            {
                0x00, 0x10, 0x70, 0x80, 0x01, 0x0e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xa7
            },
            {
                0x00, 0x10, 0x70, 0x80, 0x01, 0x0e, 0x5a, 0xb3, 0xc5, 0x41, 0x00, 0x00, 0x18, 0xdb
            });

    ASSERT_EQ(0x41C5B35A, Dev->ReadRegister(Heat_TempIn));

    SerialPort->Close();
}
