#include "iec_common.h"

#include <gmock/gmock.h>

#include "serial_port.h"

namespace
{
    class TSerialPortMock: public TSerialPort {
    public:
        TSerialPortMock(const TSerialPortSettings& settings) 
            : TSerialPort(settings)
        {}

        uint8_t ReadByte(const std::chrono::microseconds& timeout) override
        {
            return 0x81;
        }

        void WriteBytes(const uint8_t* buf, int count) override
        {
            ASSERT_THAT(std::vector<uint8_t>(buf, buf + count), ::testing::ElementsAreArray(ExpectWrite));
        }

        size_t ReadFrame(uint8_t* buf,
                         size_t count,
                         const std::chrono::microseconds& responseTimeout,
                         const std::chrono::microseconds& frameTimeout,
                         TFrameCompletePred frame_complete) override
        {
            const uint8_t b[] = {0x81, 0x82, 0x03, 0x84};
            auto l = std::min(count, sizeof(b));
            memcpy(buf, b, l);
            return l;
        }

        std::vector<uint8_t> ExpectWrite;
    };

    void CheckWriteRead(std::shared_ptr<TSerialPortMock> port,
                        std::shared_ptr<TSerialPortWithIECHack> iecPort,
                        const std::vector<uint8_t>& writeArray,
                        const std::vector<uint8_t>& expectWriteArray,
                        uint8_t expectReadByte,
                        const std::vector<uint8_t>& expectReadArray)
    {
        port->ExpectWrite = expectWriteArray;
        iecPort->WriteBytes(writeArray.data(), writeArray.size());
        ASSERT_EQ(iecPort->ReadByte(std::chrono::microseconds::zero()), expectReadByte);
        {
            std::vector<uint8_t> readBuf(expectReadArray.size(), 0);
            iecPort->ReadFrame(readBuf.data(), readBuf.size(), std::chrono::microseconds::zero(), std::chrono::microseconds::zero());
            ASSERT_THAT(readBuf, ::testing::ElementsAreArray(expectReadArray));
        }
    }

     void CheckSetSerialPortByteFormat(const TSerialPortSettings& portSettings)
    {
        auto port = std::make_shared<TSerialPortMock>(portSettings);
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
}

TEST(TIECTest, CheckStripParity)
{
    uint8_t b1[] = {0x81, 0x82, 0x03, 0x84};
    IEC::CheckStripEvenParity(b1, sizeof(b1));
    ASSERT_THAT(b1, ::testing::ElementsAreArray({0x1, 0x2, 0x3, 0x4}));

    uint8_t b2[] = {0x81, 0x2, 0x3, 0x84};
    ASSERT_THROW(IEC::CheckStripEvenParity(b2, sizeof(b2)), TSerialDeviceTransientErrorException);
}

TEST(TIECTest, SetParity)
{
    std::vector<uint8_t> src;
    for (size_t i = 0; i < 0x80; ++i) {
        src.push_back(i);
    }

    ASSERT_THAT(IEC::SetEvenParity(src.data(), src.size()),
                ::testing::ElementsAreArray({0x00, 0x81, 0x82, 0x03, 0x84, 0x05, 0x06, 0x87, 0x88, 0x09, 0x0A, 0x8B, 0x0C, 0x8D, 0x8E, 0x0F,
                                             0x90, 0x11, 0x12, 0x93, 0x14, 0x95, 0x96, 0x17, 0x18, 0x99, 0x9A, 0x1B, 0x9C, 0x1D, 0x1E, 0x9F,
                                             0xA0, 0x21, 0x22, 0xA3, 0x24, 0xA5, 0xA6, 0x27, 0x28, 0xA9, 0xAA, 0x2B, 0xAC, 0x2D, 0x2E, 0xAF,
                                             0x30, 0xB1, 0xB2, 0x33, 0xB4, 0x35, 0x36, 0xB7, 0xB8, 0x39, 0x3A, 0xBB, 0x3C, 0xBD, 0xBE, 0x3F,
                                             0xC0, 0x41, 0x42, 0xC3, 0x44, 0xC5, 0xC6, 0x47, 0x48, 0xC9, 0xCA, 0x4B, 0xCC, 0x4D, 0x4E, 0xCF,
                                             0x50, 0xD1, 0xD2, 0x53, 0xD4, 0x55, 0x56, 0xD7, 0xD8, 0x59, 0x5A, 0xDB, 0x5C, 0xDD, 0xDE, 0x5F,
                                             0x60, 0xE1, 0xE2, 0x63, 0xE4, 0x65, 0x66, 0xE7, 0xE8, 0x69, 0x6A, 0xEB, 0x6C, 0xED, 0xEE, 0x6F,
                                             0xF0, 0x71, 0x72, 0xF3, 0x74, 0xF5, 0xF6, 0x77, 0x78, 0xF9, 0xFA, 0x7B, 0xFC, 0x7D, 0x7E, 0xFF}));
}

TEST(TIECTest, SetSerialPortByteFormat)
{
    CheckSetSerialPortByteFormat(TSerialPortSettings("/dev/null", 9600, 'N', 8, 1));
    CheckSetSerialPortByteFormat(TSerialPortSettings("/dev/null", 9600, 'E', 7, 1));
}

TEST(TIECTest, WriteReadHack)
{
    TSerialPortSettings portSettings("/dev/null", 9600, 'N', 8, 1);
    auto port = std::make_shared<TSerialPortMock>(portSettings);
    auto iecPort = std::make_shared<TSerialPortWithIECHack>(port);

    std::vector<uint8_t> b({0x1, 0x2, 0x3, 0x4});
    std::vector<uint8_t> bWithParity({0x81, 0x82, 0x3, 0x84});

    CheckWriteRead(port, iecPort, b, b, 0x81, bWithParity);

    TSerialPortByteFormat newByteFormat('E', 7, 1);
    iecPort->SetSerialPortByteFormat(&newByteFormat);
    CheckWriteRead(port, iecPort, b, bWithParity, 0x01, b);

    iecPort->SetSerialPortByteFormat(nullptr);
    CheckWriteRead(port, iecPort, b, b, 0x81, bWithParity);
}
