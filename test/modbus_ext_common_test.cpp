#include "modbus_ext_common.h"
#include "gtest/gtest.h"

namespace
{
    class TPortMock: public TPort
    {
    public:
        std::vector<uint8_t> Request;
        std::vector<uint8_t> Response;

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
            Request.clear();
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
            memcpy(buf, Response.data(), Response.size());
            return Response.size();
        }

        void SkipNoise() override
        {}

        void SleepSinceLastInteraction(const std::chrono::microseconds& us) override
        {}

        std::string GetDescription(bool verbose) const override
        {
            return std::string();
        }

        std::chrono::milliseconds GetSendTime(double bytesNumber) const override
        {
            return std::chrono::milliseconds(1);
        }
    };

    class TTestEventsVisitor: public ModbusExt::IEventsVisitor
    {
    public:
        std::unordered_map<uint16_t, uint16_t> Events;

        virtual void Event(uint8_t slaveId,
                           uint8_t eventType,
                           uint16_t eventId,
                           const uint8_t* data,
                           size_t dataSize) override
        {
            Events[eventId] = data[0] | (data[1] << 8);
        }
    };
} // namespace

TEST(TModbusExtTest, EventsEnablerOneReg)
{
    TPortMock port;
    port.Response = {0x0A, 0x46, 0x18, 0x01, 0x80, 0x29, 0x7E};

    std::map<uint16_t, bool> response;
    ModbusExt::TEventsEnabler ev(10,
                                 port,
                                 std::chrono::milliseconds(100),
                                 std::chrono::milliseconds(100),
                                 [&response](uint16_t reg, bool enabled) { response[reg] = enabled; });
    ev.AddRegister(101, ModbusExt::TEventRegisterType::COIL, ModbusExt::TEventPriority::HIGH);

    try {
        ev.SendRequest();
    } catch (...) {
    }

    EXPECT_EQ(port.Request.size(), 11);
    EXPECT_EQ(port.Request[0], 0x0A); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x18); // subcommand
    EXPECT_EQ(port.Request[3], 0x05); // settings size

    EXPECT_EQ(port.Request[4], 0x01); // event type
    EXPECT_EQ(port.Request[5], 0x00);
    EXPECT_EQ(port.Request[6], 0x65); // address
    EXPECT_EQ(port.Request[7], 0x01); // count
    EXPECT_EQ(port.Request[8], 0x02); // priority

    // CRC16
    EXPECT_EQ(port.Request[9], 0xC5);
    EXPECT_EQ(port.Request[10], 0x90);

    EXPECT_EQ(response.size(), 1);
    EXPECT_TRUE(response[101]);
}

TEST(TModbusExtTest, EventsEnablerTwoRanges)
{
    TPortMock port;
    port.Response = {0x0A, 0x46, 0x18, 0x05, 0x80, 0x80, 0x80, 0x80, 0x00, 0x20, 0x28};

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

    try {
        ev.SendRequest();
    } catch (...) {
    }

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

    // CRC16
    EXPECT_EQ(port.Request[29], 0x59);
    EXPECT_EQ(port.Request[30], 0x4C);

    EXPECT_EQ(response.size(), 5);
    EXPECT_TRUE(response[101]);
    EXPECT_TRUE(response[102]);
    EXPECT_TRUE(response[103]);
    EXPECT_TRUE(response[104]);
    EXPECT_FALSE(response[105]);
}

TEST(TModbusExtTest, ReadEvents)
{
    TPortMock port;
    TTestEventsVisitor visitor;
    ModbusExt::TEventConfirmationState state = {0, 0};

    port.Response = {0xFD, 0x46, 0x14, 0xD2, 0x5F}; // No events
    try {
        ModbusExt::ReadEvents(port, std::chrono::milliseconds(100), std::chrono::milliseconds(100), visitor, state);
    } catch (...) {
    }

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // broadcast
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x00); // min slave id
    EXPECT_EQ(port.Request[4], 0xF8); // max length
    EXPECT_EQ(port.Request[5], 0x00); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x00); // flag (confirmation)
    // CRC16
    EXPECT_EQ(port.Request[7], 0x79);
    EXPECT_EQ(port.Request[8], 0x5B);

    EXPECT_EQ(visitor.Events.size(), 0);

    port.Response = {0x05, 0x46, 0x11, 0x01, 0x01, 0x06, 0x02, 0x04, 0x01, 0xD0, 0x04, 0x00, 0x2B, 0xAC};
    ModbusExt::ReadEvents(port, std::chrono::milliseconds(100), std::chrono::milliseconds(100), visitor, state);

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x00); // min slave id
    EXPECT_EQ(port.Request[4], 0xF8); // max length
    EXPECT_EQ(port.Request[5], 0x00); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x00); // flag (confirmation)
    // CRC16
    EXPECT_EQ(port.Request[7], 0x79);
    EXPECT_EQ(port.Request[8], 0x5B);

    EXPECT_EQ(visitor.Events.size(), 1);
    EXPECT_EQ(visitor.Events[464], 4);

    port.Response = {0xFD, 0x46, 0x14, 0xD2, 0x5F}; // No events
    visitor.Events.clear();
    ModbusExt::ReadEvents(port,
                          std::chrono::milliseconds(100),
                          std::chrono::milliseconds(100),
                          visitor,
                          state,
                          std::chrono::milliseconds(10));

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x00); // min slave id
    EXPECT_EQ(port.Request[4], 0x02); // max length
    EXPECT_EQ(port.Request[5], 0x05); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x01); // flag (confirmation)
    // CRC16
    EXPECT_EQ(port.Request[7], 0x9B);
    EXPECT_EQ(port.Request[8], 0xFA);

    EXPECT_EQ(visitor.Events.size(), 0);
}
