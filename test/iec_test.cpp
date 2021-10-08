#include "iec_common.h"

#include <gtest/gtest.h>

#include "serial_port.h"

namespace
{
    class TSerialPortMock: public TSerialPort
    {
    public:
        TSerialPortMock(const TSerialPortSettings& settings): TSerialPort(settings)
        {}

        uint8_t ReadByte(const std::chrono::microseconds& timeout) override
        {
            return 0x81;
        }

        void WriteBytes(const uint8_t* buf, int count) override
        {
            ASSERT_EQ(count, ExpectWrite.size());
            for (int i = 0; i < count; ++i)
                ASSERT_EQ(buf[i], ExpectWrite[i]) << i;
        }

        size_t ReadFrame(uint8_t*                         buf,
                         size_t                           count,
                         const std::chrono::microseconds& responseTimeout,
                         const std::chrono::microseconds& frameTimeout,
                         TFrameCompletePred               frame_complete) override
        {
            const uint8_t b[] = {0x81, 0x82, 0x03, 0x84};
            auto          l   = std::min(count, sizeof(b));
            memcpy(buf, b, l);
            return l;
        }

        std::vector<uint8_t> ExpectWrite;
    };

    void CheckWriteRead(std::shared_ptr<TSerialPortMock>        port,
                        std::shared_ptr<TSerialPortWithIECHack> iecPort,
                        const std::vector<uint8_t>&             writeArray,
                        const std::vector<uint8_t>&             expectWriteArray,
                        uint8_t                                 expectReadByte,
                        const std::vector<uint8_t>&             expectReadArray)
    {
        port->ExpectWrite = expectWriteArray;
        iecPort->WriteBytes(writeArray.data(), writeArray.size());
        ASSERT_EQ(iecPort->ReadByte(std::chrono::microseconds::zero()), expectReadByte);
        {
            std::vector<uint8_t> readBuf(expectReadArray.size(), 0);
            iecPort->ReadFrame(readBuf.data(),
                               readBuf.size(),
                               std::chrono::microseconds::zero(),
                               std::chrono::microseconds::zero());
            ASSERT_EQ(readBuf.size(), expectReadArray.size());
            for (size_t i = 0; i < readBuf.size(); ++i)
                ASSERT_EQ(readBuf[i], expectReadArray[i]) << i;
        }
    }

    void CheckSetSerialPortByteFormat(const TSerialPortSettings& portSettings)
    {
        auto port    = std::make_shared<TSerialPortMock>(portSettings);
        auto iecPort = std::make_shared<TSerialPortWithIECHack>(port);

        {
            TSerialPortByteFormat newByteFormat('E', 7, 1);
            iecPort->SetSerialPortByteFormat(&newByteFormat);
            iecPort->SetSerialPortByteFormat(nullptr);
        }

        {
            TSerialPortByteFormat newByteFormat('O', 7, 1);
            ASSERT_THROW(iecPort->SetSerialPortByteFormat(&newByteFormat), std::runtime_error);
        }
    }

    bool ShouldSetEvenParity(uint8_t b)
    {
        bool res = false;
        for (size_t i = 0; i < 7; ++i) {
            if (b & 0x01) {
                res = !res;
            }
            b >>= 1;
        }
        return res;
    }
}

TEST(TIECTest, CheckStripParity)
{
    uint8_t b1[] = {0x81, 0x82, 0x03, 0x84};
    IEC::CheckStripEvenParity(b1, sizeof(b1));
    for (size_t i = 0; i < sizeof(b1); ++i)
        ASSERT_EQ(b1[i], i + 1);

    uint8_t b2[] = {0x81, 0x2, 0x3, 0x84};
    ASSERT_THROW(IEC::CheckStripEvenParity(b2, sizeof(b2)), TSerialDeviceTransientErrorException);
}

TEST(TIECTest, SetParity)
{
    std::vector<uint8_t> src;
    for (size_t i = 0; i < 0x80; ++i) {
        src.push_back(i);
    }

    src = IEC::SetEvenParity(src.data(), src.size());

    for (uint8_t i = 0; i < 0x80; ++i) {
        ASSERT_EQ(src[i], (ShouldSetEvenParity(i) ? (0x80 | i) : i));
    }
}

TEST(TIECTest, SetSerialPortByteFormat)
{
    CheckSetSerialPortByteFormat(TSerialPortSettings("/dev/null", 9600, 'N', 8, 1));
    CheckSetSerialPortByteFormat(TSerialPortSettings("/dev/null", 9600, 'E', 7, 1));
}

TEST(TIECTest, WriteReadHack)
{
    TSerialPortSettings portSettings("/dev/null", 9600, 'N', 8, 1);
    auto                port    = std::make_shared<TSerialPortMock>(portSettings);
    auto                iecPort = std::make_shared<TSerialPortWithIECHack>(port);

    std::vector<uint8_t> b({0x1, 0x2, 0x3, 0x4});
    std::vector<uint8_t> bWithParity({0x81, 0x82, 0x3, 0x84});

    CheckWriteRead(port, iecPort, b, b, 0x81, bWithParity);

    TSerialPortByteFormat newByteFormat('E', 7, 1);
    iecPort->SetSerialPortByteFormat(&newByteFormat);
    CheckWriteRead(port, iecPort, b, bWithParity, 0x01, b);

    iecPort->SetSerialPortByteFormat(nullptr);
    CheckWriteRead(port, iecPort, b, b, 0x81, bWithParity);
}
