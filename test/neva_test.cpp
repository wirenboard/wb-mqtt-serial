#include <string>
#include "fake_serial_port.h"
#include "devices/neva_device.h"

namespace
{
    class TNevaExpectations: public virtual TExpectorProvider
    {
    public:
        void EnqueueStartSession(bool error = false)
        {
            if (error) {
                Expector()->Expect(
                    {   // /?00000201!<CR><LF>
                        0x2f, 0x3f, 0x30, 0x30, 0x30, 0x30, 0x30, 0x32, 0x30, 0x31, 0x21, 0x0d, 0x0a
                    },
                    std::vector<int>(),
                    __func__);
            } else {
                Expector()->Expect(
                    {   // /?00000201!<CR><LF>
                        0x2f, 0x3f, 0x30, 0x30, 0x30, 0x30, 0x30, 0x32, 0x30, 0x31, 0x21, 0x0d, 0x0a
                    },
                    {   // /TPC5NEVAMT324.2307<CR><LF>
                        0x2f, 0x44, 0x50, 0x53, 0x35, 0x4e, 0x55, 0x56, 0x41, 0x4d, 0x64, 0x33, 0x32, 0x34, 0x2e, 0x32, 0x33, 0x30, 0x37, 0x0d, 0x0a
                    },
                    __func__);
            }
        }

        void EnqueueGoToProgMode(bool error = false)
        {
            Expector()->Expect(
                {   // <ACK>051<CR><LF>
                    0x06, 0x30, 0x35, 0x31, 0x0d, 0x0a
                },
                {   // <SOH>P0<STX>(00000000)<ETX>`
                    0x01, 0x50, 0x30, 0x02, 0x28, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x29, 0x03, 0x60 + error
                }, __func__);
        }

        void EnqueueSendPassword(bool error = false)
        {
            Expector()->Expect(
                {   // <SOH>P1<STX>(00000000)<ETX>a
                    0x01, 0x50, 0x31, 0x02, 0x28, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x29, 0x03, 0x61
                },
                {   // <ACK>
                    0x06 + error
                }, __func__);
        }

        void EnqueuePollRequests()
        {
            // Total P
            Expector()->Expect(
                {   // <SOH>R1<STX>100700FF()<ETX>e
                    0x01, 0x52, 0x31, 0x02, 0x31, 0x30, 0x30, 0x37, 0x30, 0x30, 0x46, 0x46, 0x28, 0x29, 0x03, 0x65
                },
                {   // <STX>100700FF(00005.3)<ETX>,
                    0x02, 0x31, 0x30, 0x30, 0x37, 0x30, 0x30, 0x46, 0x46, 0x28, 0x30, 0x30, 0x30, 0x30, 0x35, 0x2e, 0x33, 0x29, 0x03, 0x2c
                }, __func__);

            // PF L1
            Expector()->Expect(
                {   // <SOH>R1<STX>2107FFFF()<ETX>g
                    0x01, 0x52, 0x31, 0x02, 0x32, 0x31, 0x30, 0x37, 0x46, 0x46, 0x46, 0x46, 0x28, 0x29, 0x03, 0x67
                },
                {   // <STX>2107FFFF(00.717)<ETX>
                    0x02, 0x32, 0x31, 0x30, 0x37, 0x46, 0x46, 0x46, 0x46, 0x28, 0x30, 0x30, 0x2e, 0x37, 0x31, 0x37, 0x29, 0x03, 0x19
                }, __func__);

            // Temperature
            Expector()->Expect(
                {   // <SOH>R1<STX>600900FF()<ETX>l
                    0x01, 0x52, 0x31, 0x02, 0x36, 0x30, 0x30, 0x39, 0x30, 0x30, 0x46, 0x46, 0x28, 0x29, 0x03, 0x6c
                },
                {   // <STX>600900FF(029)<ETX>6
                    0x02, 0x36, 0x30, 0x30, 0x39, 0x30, 0x30, 0x46, 0x46, 0x28, 0x30, 0x32, 0x39, 0x29, 0x03, 0x36
                }, __func__);

            // Total RP energy
            Expector()->Expect(
                {   // <SOH>R1<STX>030880FF()<ETX>`
                    0x01, 0x52, 0x31, 0x02, 0x30, 0x33, 0x30, 0x38, 0x38, 0x30, 0x46, 0x46, 0x28, 0x29, 0x03, 0x60
                },
                {   // <STX>(01)<ETX><ETX>
                    0x02, 0x28, 0x30, 0x31, 0x29, 0x03, 0x03
                }, __func__);

            // Total A energy
            Expector()->Expect(
                {   // <SOH>R1<STX>0F0880FF()<ETX><NAK>
                    0x01, 0x52, 0x31, 0x02, 0x30, 0x46, 0x30, 0x38, 0x38, 0x30, 0x46, 0x46, 0x28, 0x29, 0x03, 0x15
                },
                {   // <STX>0F0880FF(000046.24,000031.14,000015.10,000000.00,000000.00)<ETX>0x5c
                    0x02, 0x30, 0x46, 0x30, 0x38, 0x38, 0x30, 0x46, 0x46, 0x28, 0x30, 0x30, 0x30, 0x30, 0x34, 0x36,
                    0x2e, 0x32, 0x34, 0x2c, 0x30, 0x30, 0x30, 0x30, 0x33, 0x31, 0x2e, 0x31, 0x34, 0x2c, 0x30, 0x30,
                    0x30, 0x30, 0x31, 0x35, 0x2e, 0x31, 0x30, 0x2c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x2e, 0x30,
                    0x30, 0x2c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x2e, 0x30, 0x30, 0x29, 0x03, 0x5c
                }, __func__);
        }
    };

    class TNevaTest: public TSerialDeviceTest, public TNevaExpectations
    {
    protected:
        std::shared_ptr<TNevaDevice> Dev;

        void SetUp()
        {
            TSerialDeviceTest::SetUp();
            Dev = std::make_shared<TNevaDevice>(GetDeviceConfig(), SerialPort, TSerialDeviceFactory::GetProtocol("neva"));
            SerialPort->Open();
        }

        PDeviceConfig GetDeviceConfig()
        {
            return std::make_shared<TDeviceConfig>("neva32x", "00000201", "neva");
        }
    };

    class TNevaIntegrationTest: public TSerialDeviceIntegrationTest, public TNevaExpectations
    {
    protected:
        const char* ConfigPath() const { return "configs/config-neva-test.json"; }
        std::string GetTemplatePath() const override { return "../wb-mqtt-serial-templates"; }
    };
}

TEST_F(TNevaTest, PrepareOk)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();

    Dev->Prepare();
    SerialPort->Close();
}

TEST_F(TNevaTest, StartSessionError)
{
    for (size_t i = 0; i < 5; ++i) {
        EnqueueStartSession(true);
    }

    ASSERT_THROW(Dev->Prepare(), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaTest, SwitchToProgModeError)
{
    for (size_t i = 0; i < 5; ++i) {
        EnqueueStartSession();
        EnqueueGoToProgMode(true);
    }
    ASSERT_THROW(Dev->Prepare(), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaTest, SendPasswordError)
{
    for (size_t i = 0; i < 5; ++i) {
        EnqueueStartSession();
        EnqueueGoToProgMode();
        EnqueueSendPassword(true);
    }

    ASSERT_THROW(Dev->Prepare(), TSerialDeviceTransientErrorException);
    SerialPort->Close();
}

TEST_F(TNevaIntegrationTest, Poll)
{
    EnqueueStartSession();
    EnqueueGoToProgMode();
    EnqueueSendPassword();
    EnqueuePollRequests();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}
