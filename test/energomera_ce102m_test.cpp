#include "devices/energomera_iec_mode_c_device.h"
#include "fake_serial_port.h"
#include <string>

namespace
{
    class TEnergomeraCe102MExpectations: public virtual TExpectorProvider
    {
    public:
        void EnqueueStartSession(const std::string& identification = "/EKT5CE102Mv01\r\n")
        {
            Expector()->Expect(ExpectVectorFromString("/?141628345!\r\n"),
                               ExpectVectorFromString(identification),
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
                                                      "B0\x03\x75"),
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

    class TEnergomeraCe102MTest: public TSerialDeviceTest, public TEnergomeraCe102MExpectations
    {
    protected:
        std::shared_ptr<TEnergomeraIecModeCDevice> Dev;

        void SetUp()
        {
            TSerialDeviceTest::SetUp();
            Dev = std::make_shared<TEnergomeraIecModeCDevice>(GetDeviceConfig(),
                                                              DeviceFactory.GetProtocol("energomera_iec_mode_c"));
            SerialPort->Open();
        }

        PDeviceConfig GetDeviceConfig() const
        {
            auto cfg = std::make_shared<TDeviceConfig>("ce102m", "141628345", "energomera_iec_mode_c");
            cfg->FrameTimeout = std::chrono::milliseconds(100);
            cfg->Password = {0x77, 0x77, 0x77};
            return cfg;
        }

        void Session(const std::string& identification)
        {
            EnqueueStartSession(identification);
            EnqueueGoToProgMode();
            EnqueueSendPassword();
            EnqueueEndSession();
            Dev->Prepare(*SerialPort);
            Dev->EndSession(*SerialPort);
        }
    };

    class TEnergomeraCe102MIntegrationTest: public TSerialDeviceIntegrationTest, public TEnergomeraCe102MExpectations
    {
    protected:
        const char* ConfigPath() const override
        {
            return "configs/config-energomera-ce102m-test.json";
        }
    };
}

TEST_F(TEnergomeraCe102MTest, PauseBeforeMessage)
{
    // Upper case third letter of manufacturer's identification: reaction time is at least 200 ms,
    // frame timeout (100 ms) before every message
    Note() << "/EKT";
    Session("/EKT5CE102Mv01\r\n");

    // Lower case third letter: reaction time is at least 20 ms,
    // 30 ms before messages after the identification
    Note() << "/EKt";
    Session("/EKt5CE102Mv01\r\n");

    // A new session starts with frame timeout again
    Note() << "/EKT";
    Session("/EKT5CE102Mv01\r\n");
    SerialPort->Close();
}

TEST_F(TEnergomeraCe102MIntegrationTest, Poll)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();
    EnqueuePollRequests();
    // All parameters are read in one session within one poll cycle
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
