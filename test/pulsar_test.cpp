#include "testlog.h"
#include "fake_serial_port.h"
#include "pulsar_device.h"

namespace {
    PSlaveEntry HeatMeter = TSlaveEntry::Intern("pulsar", 102030);

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
    // >> 00 10 70 80 01 0e 04 00 00 00 fc 90 3d cb
    // << 00 10 70 80 01 0E 5A B3 C5 41 FC 90 59 B7
    // temperature == 24.71257

    SerialPort->Expect(
            {
                0x00, 0x10, 0x70, 0x80, 0x01, 0x0e, 0x04, 0x00, 0x00, 0x00, 0xfc, 0x90, 0x3d, 0xcb
            },
            {
                0x00, 0x10, 0x70, 0x80, 0x01, 0x0e, 0x5a, 0xb3, 0xc5, 0x41, 0xfc, 0x90, 0x59, 0xb7
            });

    ASSERT_EQ(0xC5B35A0E, Dev->ReadRegister(Heat_TempIn));

    SerialPort->Close();
}
