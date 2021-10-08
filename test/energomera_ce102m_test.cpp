#include <string>
#include "fake_serial_port.h"
#include "devices/energomera_iec_mode_c_device.h"

namespace
{
    class TEnergomeraCe102MExpectations: public virtual TExpectorProvider
    {
    public:
        void EnqueueStartSession()
        {
            Expector()->Expect(ExpectVectorFromString("/?141628345!\r\n"),
                               ExpectVectorFromString("/EKT5CE102Mv01\r\n"),
                               __func__);
        }

        void EnqueueGoToProgMode()
        {
            Expector()->Expect(ExpectVectorFromString("\x06"
                                                      "051\r\n"),
                               ExpectVectorFromString("\x01P0\x02(141628345)\x03\x28"),
                               __func__);
        }

        void EnqueueSendPassword()
        {
            Expector()->Expect(ExpectVectorFromString("\x01P1\x02(777777)\x03\x21"),
                               {
                                   0x06 // <ACK>
                               },
                               __func__);
        }

        void EnqueueEndSession()
        {
            Expector()->Expect(ExpectVectorFromString("\x01"
                                                      "B0\x03\x71"),
                               {},
                               __func__);
        }

        void EnqueuePollRequests()
        {
            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "CURRE()\x03\x5a"),
                               ExpectVectorFromString("\x02"
                                                      "CURRE(0.402)\r\n\x03\x60"),
                               __func__);

            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "FREQU()\x03\x5c"),
                               ExpectVectorFromString("\x02"
                                                      "FREQU(49.97)\r\n\x03\x79"),
                               __func__);

            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "VOLTA()\x03\x5f"),
                               ExpectVectorFromString("\x02"
                                                      "VOLTA(237.58)\r\n\x03\x28"),
                               __func__);

            Expector()->Expect(
                ExpectVectorFromString("\x01R1\x02"
                                       "ET0PE()\x03\x37"),
                ExpectVectorFromString("\x02"
                                       "ET0PE(68.42)\r\n(45.54)\r\n(22.88)\r\n(0.00)\r\n(0.00)\r\n(0.00)\r\n\x03\x0f"),
                __func__);
        }
    };

    class TEnergomeraCe102MIntegrationTest: public TSerialDeviceIntegrationTest, public TEnergomeraCe102MExpectations
    {
    protected:
        const char* ConfigPath() const
        {
            return "configs/config-energomera-ce102m-test.json";
        }
    };
}

TEST_F(TEnergomeraCe102MIntegrationTest, Poll)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();
    EnqueuePollRequests();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
