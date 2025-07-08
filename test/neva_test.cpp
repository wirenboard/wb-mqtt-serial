#include "devices/neva_device.h"
#include "fake_serial_port.h"
#include <string>

namespace
{
    class TNevaExpectations: public virtual TExpectorProvider
    {
    public:
        void EnqueueStartSession(bool error = false)
        {
            if (error) {
                Expector()->Expect(ExpectVectorFromString("/?00000201!\r\n"), std::vector<int>(), __func__);
            } else {
                Expector()->Expect(ExpectVectorFromString("/?00000201!\r\n"),
                                   ExpectVectorFromString("/TPC5NEVAMT324.2307\r\n"),
                                   __func__);
            }
        }

        void EnqueueGoToProgMode(bool error = false)
        {
            Expector()->Expect(ExpectVectorFromString("\x06"
                                                      "051\r\n"),
                               (error ? ExpectVectorFromString("\x01P0\x02(00000000)\x03\x61")
                                      : ExpectVectorFromString("\x01P0\x02(00000000)\x03\x60")),
                               __func__);
        }

        void EnqueueSendPassword(bool error = false)
        {
            Expector()->Expect(ExpectVectorFromString("\x01P1\x02(00000000)\x03\x61"),
                               {
                                   0x06 + error // <ACK>
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
            // Total P
            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "100700FF()\x03\x65"),
                               ExpectVectorFromString("\x02"
                                                      "100700FF(00005.3)\x03\x2c"),
                               __func__);

            // PF L1
            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "2107FFFF()\x03\x67"),
                               ExpectVectorFromString("\x02"
                                                      "2107FFFF(00.717)\x03\x19"),
                               __func__);

            // Temperature
            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "600900FF()\x03\x6c"),
                               ExpectVectorFromString("\x02"
                                                      "600900FF(029)\x03\x36"),
                               __func__);

            // Total RP energy
            Expector()->Expect(ExpectVectorFromString("\x01R1\x02"
                                                      "030880FF()\x03\x60"),
                               ExpectVectorFromString("\x02(01)\x03\x03"),
                               __func__);

            // Total A energy
            Expector()->Expect(
                ExpectVectorFromString("\x01R1\x02"
                                       "0F0880FF()\x03\x15"),
                ExpectVectorFromString("\x02"
                                       "0F0880FF(000046.24,000031.14,000015.10,000000.00,000000.00)\x03\x5c"),
                __func__);
        }
    };

    class TNevaTest: public TSerialDeviceTest, public TNevaExpectations
    {
    protected:
        std::shared_ptr<TNevaDevice> Dev;

        void SetUp()
        {
            TSerialDeviceTest::SetUp();
            Dev = std::make_shared<TNevaDevice>(GetDeviceConfig(), DeviceFactory.GetProtocol("neva"));
            SerialPort->Open();
        }

        PDeviceConfig GetDeviceConfig() const
        {
            auto cfg = std::make_shared<TDeviceConfig>("neva32x", "00000201", "neva");
            cfg->FrameTimeout = std::chrono::milliseconds::zero();
            return cfg;
        }
    };

    class TNevaIntegrationTest: public TSerialDeviceIntegrationTest, public TNevaExpectations
    {
    protected:
        const char* ConfigPath() const override
        {
            return "configs/config-neva-test.json";
        }
    };
}

TEST_F(TNevaTest, PrepareOk)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();

    Dev->Prepare(*SerialPort);
    SerialPort->Close();
}

TEST_F(TNevaTest, StartSessionError)
{
    for (size_t i = 0; i < 5; ++i) {
        EnqueueStartSession(true);
        EnqueueEndSession();
    }

    ASSERT_THROW(Dev->Prepare(*SerialPort), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaTest, SwitchToProgModeError)
{
    EnqueueStartSession();
    EnqueueGoToProgMode(true);
    EnqueueEndSession();
    ASSERT_THROW(Dev->Prepare(*SerialPort), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaTest, SendPasswordError)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword(true);
    EnqueueEndSession();

    ASSERT_THROW(Dev->Prepare(*SerialPort), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaIntegrationTest, Poll)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();
    EnqueuePollRequests();
    for (auto i = 0; i < 5; ++i) {
        Note() << "LoopOnce()";
        SerialDriver->LoopOnce();
    }
}
