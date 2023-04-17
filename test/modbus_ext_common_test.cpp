#include "modbus_ext_common.h"
#include "gtest/gtest.h"

namespace
{
    class TPortMock: public TPort
    {
    public:
        std::vector<uint8_t> Request;

        TPortMock()
        {}

        void Open() override
        {}
        void Close() override
        {}
        bool IsOpen() const override
        {
            return false;
        }
        void CheckPortOpen() const override
        {}

        void WriteBytes(const uint8_t* buf, int count) override
        {
            Request.insert(Request.end(), buf, buf + count);
        }

        uint8_t ReadByte(const std::chrono::microseconds& timeout) override
        {
            return 0;
        }

        size_t ReadFrame(uint8_t* buf,
                         size_t count,
                         const std::chrono::microseconds& responseTimeout,
                         const std::chrono::microseconds& frameTimeout,
                         TFrameCompletePred frame_complete = 0) override
        {
            buf[0] = 0x0A; // slave id
            buf[1] = 0x46; // command
            buf[2] = 0x18; // subcommand
            buf[3] = 0x05; // data size
            buf[4] = 0x80; // 101 enabled (1000 0000)
            buf[5] = 0x80; // 102 enabled (1000 0000)
            buf[6] = 0x80; // 103 enabled (1000 0000)
            buf[7] = 0x80; // 104 enabled (1000 0000)
            buf[8] = 0x00; // 105 disabled (0000 0000)
            // CRC
            buf[9] = 0x20;
            buf[10] = 0x28;

            return 11;
        }

        void SkipNoise() override
        {}

        void SleepSinceLastInteraction(const std::chrono::microseconds& us) override
        {}

        std::string GetDescription(bool verbose) const override
        {
            return std::string();
        }
    };
}

TEST(TModbusExtTest, EventsEnabler)
{
    TPortMock port;
    std::map<uint16_t, bool> response;
    ModbusExt::TEventsEnabler ev(10,
                                 port,
                                 std::chrono::milliseconds(100),
                                 std::chrono::milliseconds(100),
                                 [&response](uint16_t reg, bool enabled) { response[reg] = enabled; });
    ev.AddRegister(101, ModbusExt::TEventRegisterType::COIL, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(102, ModbusExt::TEventRegisterType::COIL, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(103, ModbusExt::TEventRegisterType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(104, ModbusExt::TEventRegisterType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(105, ModbusExt::TEventRegisterType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.SendRequest();

    EXPECT_EQ(port.Request.size(), 31);
    EXPECT_EQ(port.Request[0], 0x0A); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x18); // subcommand
    EXPECT_EQ(port.Request[3], 0x19); // settings size

    EXPECT_EQ(port.Request[4], 0x01); // event type
    EXPECT_EQ(port.Request[5], 0x00);
    EXPECT_EQ(port.Request[6], 0x65); // address
    EXPECT_EQ(port.Request[7], 0x01); // count
    EXPECT_EQ(port.Request[8], 0x02); // priority

    EXPECT_EQ(port.Request[9], 0x01); // event type
    EXPECT_EQ(port.Request[10], 0x00);
    EXPECT_EQ(port.Request[11], 0x66); // address
    EXPECT_EQ(port.Request[12], 0x01); // count
    EXPECT_EQ(port.Request[13], 0x02); // priority

    EXPECT_EQ(port.Request[14], 0x04); // event type
    EXPECT_EQ(port.Request[15], 0x00);
    EXPECT_EQ(port.Request[16], 0x67); // address
    EXPECT_EQ(port.Request[17], 0x01); // count
    EXPECT_EQ(port.Request[18], 0x01); // priority

    EXPECT_EQ(port.Request[19], 0x04); // event type
    EXPECT_EQ(port.Request[20], 0x00);
    EXPECT_EQ(port.Request[21], 0x68); // address
    EXPECT_EQ(port.Request[22], 0x01); // count
    EXPECT_EQ(port.Request[23], 0x01); // priority

    EXPECT_EQ(port.Request[24], 0x04); // event type
    EXPECT_EQ(port.Request[25], 0x00);
    EXPECT_EQ(port.Request[26], 0x69); // address
    EXPECT_EQ(port.Request[27], 0x01); // count
    EXPECT_EQ(port.Request[28], 0x01); // priority

    // CRC
    EXPECT_EQ(port.Request[29], 0x59);
    EXPECT_EQ(port.Request[30], 0x4C);

    EXPECT_TRUE(response[101]);
    EXPECT_TRUE(response[102]);
    EXPECT_TRUE(response[103]);
    EXPECT_TRUE(response[104]);
    EXPECT_FALSE(response[105]);
}
