#include "devices/ivtm_device.h"
#include "fake_serial_port.h"
#include <string>
#include <wblib/testing/testlog.h>

class TIVTMDeviceTest: public TSerialDeviceTest
{
protected:
    void SetUp();
    PIVTMDevice Dev;

    PRegister Dev1Temp;
    PRegister Dev1Humidity;
    PRegister Dev2Temp;
};

void TIVTMDeviceTest::SetUp()
{
    TSerialDeviceTest::SetUp();
    // Device should not depend on slave id from DeviceConfig
    Dev = std::make_shared<TIVTMDevice>(std::make_shared<TDeviceConfig>("ivtm", std::to_string(0x0001), "ivtm"),
                                        SerialPort,
                                        DeviceFactory.GetProtocol("ivtm"));

    Dev1Temp = TRegister::Intern(Dev, TRegisterConfig::Create(0, 0, Float));
    Dev1Humidity = TRegister::Intern(Dev, TRegisterConfig::Create(0, 4, Float));
    Dev2Temp = TRegister::Intern(Dev, TRegisterConfig::Create(0, 0, Float));

    SerialPort->Open();
}

TEST_F(TIVTMDeviceTest, IVTM7MQuery)
{
    // >> 24 30 30 30 31 52 52 30 30 30 30 30 34 41 44 0d
    // << 21 30 30 30 31 52 52 43 45 44 33 44 31 34 31 35 46 0D
    // temperature == 26.228420

    SerialPort->Expect({'$', '0', '0', '0', '1', 'R', 'R', '0', '0', '0', '0', '0', '4', 'A', 'D', 0x0d},
                       {
                           // Session setup response
                           '!', // header
                           '0',
                           '0',
                           '0',
                           '1', // slave addr
                           'R',
                           'R', // read response
                           'C',
                           'E',
                           'D',
                           '3',
                           'D',
                           '1',
                           '4',
                           '1', // temp data CE D3 D1 41 (little endian)
                           '5',
                           'F', // CRC
                           0x0d // footer
                       });

    ASSERT_EQ(TRegisterValue{0x41D1D3CE}, Dev->ReadRegisterImpl(Dev1Temp)); // big-endian

    // >> 24 30 30 30 31 52 52 30 30 30 34 30 34 42 31 0d
    // << 21 30 30 30 31 52 52 33 30 39 41 45 42 34 31 34 46 0D
    // humidity == 29.450287

    SerialPort->Expect({'$', '0', '0', '0', '1', 'R', 'R', '0', '0', '0', '4', '0', '4', 'B', '1', 0x0d},
                       {
                           // Session setup response
                           '!', // header
                           '0',
                           '0',
                           '0',
                           '1', // slave addr
                           'R',
                           'R', // read response
                           '3',
                           '0',
                           '9',
                           'A',
                           'E',
                           'B',
                           '4',
                           '1', // hum data 30 9A EB 41 (little endian)
                           '4',
                           'F', // CRC
                           0x0D // footer
                       });

    ASSERT_EQ(TRegisterValue{0x41EB9A30}, Dev->ReadRegisterImpl(Dev1Humidity)); // big-endian

    // Test upper-case hex letters

    // >> 24 30 30 30 31 52 52 30 30 30 30 30 34 41 44 0d
    // << 21 30 30 30 31 52 52 35 45 38 35 43 37 34 31 34 43 0D
    // temperature == 24.940121

    SerialPort->Expect({'$', '0', '0', '0', '1', 'R', 'R', '0', '0', '0', '0', '0', '4', 'A', 'D', 0x0d},
                       {
                           // Session setup response
                           '!', // header
                           '0',
                           '0',
                           '0',
                           '1', // slave addr
                           'R',
                           'R', // read response
                           '5',
                           'E',
                           '8',
                           '5',
                           'C',
                           '7',
                           '4',
                           '1', // temp data 5E 85 C7 41 (little endian)
                           '4',
                           'C', // CRC
                           0x0d // footer
                       });

    ASSERT_EQ(TRegisterValue{0x41C7855E}, Dev->ReadRegisterImpl(Dev2Temp)); // big-endian
    SerialPort->Close();
}
