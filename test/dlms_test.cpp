#include <string>
#include <wblib/testing/testlog.h>
#include "fake_serial_port.h"
#include "devices/uniel_device.h"
#include "uniel_expectations.h"

class TDlmsDeviceExpectations: public virtual TExpectorProvider
{
protected:
    void EnqueueRequests()
    {
        // Connection
        Expector()->Expect(
            { 0x7e, 0xa0, 0x08, 0x02, 0x29, 0x41, 0x53, 0x9e, 0xb4, 0x7e },
            { 0x7e, 0xa0, 0x21, 0x41, 0x02, 0x29, 0x73, 0x1b, 0x16, 0x81, 0x80, 0x14, 0x05, 0x02, 0x00, 0x80, 0x06, 0x02, 0x00, 0x80, 0x07, 0x04, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0x01, 0xce, 0x6a, 0x7e },
            __func__);

        Expector()->Expect(
            { 0x7e, 0xa0, 0x08, 0x02, 0x29, 0x41, 0x93, 0x92, 0x72, 0x7e },
            { 0x7e, 0xa0, 0x21, 0x41, 0x02, 0x29, 0x73, 0x1b, 0x16, 0x81, 0x80, 0x14, 0x05, 0x02, 0x00, 0x80, 0x06, 0x02, 0x00, 0x80, 0x07, 0x04, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0x01, 0xce, 0x6a, 0x7e },
            __func__);

        Expector()->Expect(
            { 0x7e, 0xa0, 0x43, 0x02, 0x29, 0x41, 0x10, 0xcf, 0x42, 0xe6, 0xe6, 0x00, 0x60, 0x34, 0xa1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x01, 0x01, 0x8a, 0x02, 0x07, 0x80, 0x8b, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x02, 0x01, 0xac, 0x08, 0x80, 0x06, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0xbe, 0x10, 0x04, 0x0e, 0x01, 0x00, 0x00, 0x00, 0x06, 0x5f, 0x1f, 0x04, 0x00, 0x00, 0x1e, 0x5d, 0xff, 0xff, 0xa0, 0xfd, 0x7e },
            { 0x7e, 0xa0, 0x38, 0x41, 0x02, 0x29, 0x30, 0xa0, 0x83, 0xe6, 0xe7, 0x00, 0x61, 0x29, 0xa1, 0x09, 0x06, 0x07, 0x60, 0x85, 0x74, 0x05, 0x08, 0x01, 0x01, 0xa2, 0x03, 0x02, 0x01, 0x00, 0xa3, 0x05, 0xa1, 0x03, 0x02, 0x01, 0x00, 0xbe, 0x10, 0x04, 0x0e, 0x08, 0x00, 0x06, 0x5f, 0x1f, 0x04, 0x00, 0x00, 0x10, 0x1d, 0x00, 0x64, 0x00, 0x07, 0x82, 0x8b, 0x7e },
            __func__);

        // OBIS request
        Expector()->Expect(
            { 0x7e, 0xa0, 0x1a, 0x02, 0x29, 0x41, 0x32, 0xd9, 0x64, 0xe6, 0xe6, 0x00, 0xc0, 0x01, 0x81, 0x00, 0x03, 0x00, 0x00, 0x60, 0x09, 0x00, 0xff, 0x03, 0x00, 0x8e, 0xb5, 0x7e },
            { 0x7e, 0xa0, 0x17, 0x41, 0x02, 0x29, 0x52, 0xd9, 0xc9, 0xe6, 0xe7, 0x00, 0xc4, 0x01, 0x81, 0x00, 0x02, 0x02, 0x0f, 0x00, 0x16, 0x09, 0xae, 0xd5, 0x7e },
            __func__);

        Expector()->Expect(
            { 0x7e, 0xa0, 0x1a, 0x02, 0x29, 0x41, 0x54, 0xe9, 0x62, 0xe6, 0xe6, 0x00, 0xc0, 0x01, 0x81, 0x00, 0x03, 0x00, 0x00, 0x60, 0x09, 0x00, 0xff, 0x02, 0x00, 0x56, 0xac, 0x7e },
            { 0x7e, 0xa0, 0x14, 0x41, 0x02, 0x29, 0x74, 0x21, 0x90, 0xe6, 0xe7, 0x00, 0xc4, 0x01, 0x81, 0x00, 0x10, 0x00, 0x1e, 0x66, 0x3c, 0x7e },
            __func__);
    }
};

class TDlmsIntegrationTest: public TSerialDeviceIntegrationTest, public TDlmsDeviceExpectations
{
protected:
    void TearDown();
    const char* ConfigPath() const { return "configs/config-dlms-test.json"; }
};

void TDlmsIntegrationTest::TearDown()
{
    SerialPort->DumpWhatWasRead();
    TSerialDeviceIntegrationTest::TearDown();
}

TEST_F(TDlmsIntegrationTest, Poll)
{
    ASSERT_TRUE(!!SerialPort);

    EnqueueRequests();

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
