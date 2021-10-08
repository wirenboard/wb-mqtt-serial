#include <string>
#include "fake_serial_port.h"
#include "devices/lls_device.h"

class TLLSIntegrationTest: public TSerialDeviceIntegrationTest
{
protected:
    void        SetUp();
    void        TearDown();
    const char* ConfigPath() const
    {
        return "configs/config-lls-test.json";
    }
    std::string GetTemplatePath() const override
    {
        return "../wb-mqtt-serial-templates/";
    }
    void EnqueeCmdF0Response();
    void EnqueeCmdFCResponse();
};

void TLLSIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    ASSERT_TRUE(!!SerialPort);
}

void TLLSIntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TLLSIntegrationTest::EnqueeCmdF0Response()
{
    Expector()->Expect({0x31, 0x01, 0xF0, 0xC5},
                       {
                           0x3E, 0x01, 0xF0, 0x1C, 0x06, 0x00, 0xA6, 0x13, 0x00, 0x00, 0x9E, 0x11,
                           0x00, 0x00, 0xA0, 0x86, 0x01, 0x00, 0xD4, 0x05, 0x00, 0x0A, 0x05, 0x68,
                       },
                       __func__);
}

void TLLSIntegrationTest::EnqueeCmdFCResponse()
{
    Expector()->Expect(
        {
            0x31,
            0x01,
            0xfc,
            0x66,
        },
        {
            0x3E,
            0x01,
            0xFC,
            0x54,
            0x57,
            0x00,
            0x00,
            0xB0,
            0x00,
            0x4F,
        },
        __func__);
}

TEST_F(TLLSIntegrationTest, Poll)
{
    EnqueeCmdF0Response();
    EnqueeCmdFCResponse();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}