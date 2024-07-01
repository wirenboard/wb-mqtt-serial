#include "crc16.h"
#include "modbus_ext_common.h"
#include "serial_exc.h"
#include "gtest/gtest.h"

#include <cmath>

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

        TReadFrameResult ReadFrame(uint8_t* buf,
                                   size_t count,
                                   const std::chrono::microseconds& responseTimeout,
                                   const std::chrono::microseconds& frameTimeout,
                                   TFrameCompletePred frame_complete = 0) override
        {
            if (Response.size() > count) {
                throw std::runtime_error("Buffer is too small");
            }
            TReadFrameResult res;
            res.Count = Response.size();
            memcpy(buf, Response.data(), Response.size());
            return res;
        }

        void SkipNoise() override
        {}

        void SleepSinceLastInteraction(const std::chrono::microseconds& us) override
        {}

        std::string GetDescription(bool verbose) const override
        {
            return std::string();
        }

        std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override
        {
            // 115200 8-N-2
            auto us = std::ceil((1000000.0 * 11 * bytesNumber) / 115200.0);
            return std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>(us));
        }
    };

    class TTestEventsVisitor: public ModbusExt::IEventsVisitor
    {
    public:
        std::unordered_map<uint16_t, uint16_t> Events;
        bool Reboot = false;

        virtual void Event(uint8_t slaveId,
                           uint8_t eventType,
                           uint16_t eventId,
                           const uint8_t* data,
                           size_t dataSize) override
        {
            if (eventType == ModbusExt::TEventType::REBOOT) {
                Reboot = true;
                return;
            }
            Events[eventId] = data[0] | (data[1] << 8);
        }
    };

    void SetCrc(std::vector<uint8_t>& data)
    {
        if (data.size() < 2) {
            return;
        }
        auto crc = CRC16::CalculateCRC16(data.data(), data.size() - 2);
        data[data.size() - 1] = crc & 0xFF;
        data[data.size() - 2] = (crc >> 8) & 0xFF;
    }

} // namespace

TEST(TModbusExtTest, EventsEnablerOneReg)
{
    TPortMock port;
    port.Response = {0x0A, 0x46, 0x18, 0x01, 0x01, 0xE9, 0x1E};

    std::map<uint16_t, bool> response;
    ModbusExt::TEventsEnabler ev(10, port, [&response](uint8_t type, uint16_t reg, bool enabled) {
        response[reg] = enabled;
    });
    ev.AddRegister(101, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);

    EXPECT_NO_THROW(ev.SendRequests());

    EXPECT_EQ(port.Request.size(), 11);
    EXPECT_EQ(port.Request[0], 0x0A); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x18); // subcommand
    EXPECT_EQ(port.Request[3], 0x05); // settings size

    EXPECT_EQ(port.Request[4], 0x01); // event type
    EXPECT_EQ(port.Request[5], 0x00); // address MSB
    EXPECT_EQ(port.Request[6], 0x65); // address LSB
    EXPECT_EQ(port.Request[7], 0x01); // count
    EXPECT_EQ(port.Request[8], 0x02); // priority

    EXPECT_EQ(port.Request[9], 0xC5);  // CRC16 LSB
    EXPECT_EQ(port.Request[10], 0x90); // CRC16 MSB

    EXPECT_EQ(response.size(), 1);
    EXPECT_TRUE(response[101]);
}

TEST(TModbusExtTest, EventsEnablerIllegalFunction)
{
    TPortMock port;
    port.Response = {0x0A, 0xC6, 0x01, 0xC3, 0xA2};

    ModbusExt::TEventsEnabler ev(10, port, [](uint8_t, uint16_t, bool) {});
    ev.AddRegister(101, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);

    EXPECT_THROW(ev.SendRequests(), TSerialDevicePermanentRegisterException);
}

TEST(TModbusExtTest, EventsEnablerTwoRanges)
{
    TPortMock port;
    // 0x03 = 0b00000011
    // 0xFB = 0b11111011
    // 0x02 = 0b00000010
    port.Response = {0x0A, 0x46, 0x18, 0x03, 0x03, 0xFB, 0x02, 0xAD, 0x11};

    std::map<uint16_t, bool> response;
    ModbusExt::TEventsEnabler ev(10, port, [&response](uint8_t type, uint16_t reg, bool enabled) {
        response[reg] = enabled;
    });

    ev.AddRegister(101, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(102, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);

    ev.AddRegister(103, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(104, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(105, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(106, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(107, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(108, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(109, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(110, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(111, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(112, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);

    EXPECT_NO_THROW(ev.SendRequests());

    EXPECT_EQ(port.Request.size(), 26);
    EXPECT_EQ(port.Request[0], 0x0A); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x18); // subcommand
    EXPECT_EQ(port.Request[3], 0x14); // settings size

    EXPECT_EQ(port.Request[4], 0x01); // event type
    EXPECT_EQ(port.Request[5], 0x00); // address MSB
    EXPECT_EQ(port.Request[6], 0x65); // address LSB
    EXPECT_EQ(port.Request[7], 0x02); // count
    EXPECT_EQ(port.Request[8], 0x02); // priority 101
    EXPECT_EQ(port.Request[9], 0x02); // priority 102

    EXPECT_EQ(port.Request[10], 0x04); // event type
    EXPECT_EQ(port.Request[11], 0x00); // address MSB
    EXPECT_EQ(port.Request[12], 0x67); // address LSB
    EXPECT_EQ(port.Request[13], 0x0A); // count
    EXPECT_EQ(port.Request[14], 0x01); // priority 103
    EXPECT_EQ(port.Request[15], 0x01); // priority 104
    EXPECT_EQ(port.Request[16], 0x01); // priority 105
    EXPECT_EQ(port.Request[17], 0x01); // priority 106
    EXPECT_EQ(port.Request[18], 0x01); // priority 107
    EXPECT_EQ(port.Request[19], 0x01); // priority 108
    EXPECT_EQ(port.Request[20], 0x01); // priority 109
    EXPECT_EQ(port.Request[21], 0x02); // priority 110
    EXPECT_EQ(port.Request[22], 0x02); // priority 111
    EXPECT_EQ(port.Request[23], 0x02); // priority 112

    EXPECT_EQ(port.Request[24], 0x25); // CRC16 LSB
    EXPECT_EQ(port.Request[25], 0xF0); // CRC16 MSB

    EXPECT_EQ(response.size(), 12);
    EXPECT_TRUE(response[101]);
    EXPECT_TRUE(response[102]);
    EXPECT_TRUE(response[103]);
    EXPECT_TRUE(response[104]);
    EXPECT_FALSE(response[105]);
    EXPECT_TRUE(response[106]);
    EXPECT_TRUE(response[107]);
    EXPECT_TRUE(response[108]);
    EXPECT_TRUE(response[109]);
    EXPECT_TRUE(response[110]);
    EXPECT_FALSE(response[111]);
    EXPECT_TRUE(response[112]);
}

TEST(TModbusExtTest, EventsEnablerRangesWithHoles)
{
    TPortMock port;
    // 0x03 = 0b00000011
    // 0xF3 = 0b11110011
    // 0x02 = 0b00000010
    // 0x03 = 0b00000011
    port.Response = {0x0A, 0x46, 0x18, 0x04, 0x03, 0xF3, 0x02, 0x03, 0xA4, 0xBE};

    std::map<uint16_t, bool> response;
    ModbusExt::TEventsEnabler ev(
        10,
        port,
        [&response](uint8_t type, uint16_t reg, bool enabled) { response[reg] = enabled; },
        ModbusExt::TEventsEnabler::DISABLE_EVENTS_IN_HOLES);

    ev.AddRegister(101, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(102, ModbusExt::TEventType::COIL, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(103, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(104, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(107, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(108, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(109, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::LOW);
    ev.AddRegister(110, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(111, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(112, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(118, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);
    ev.AddRegister(119, ModbusExt::TEventType::INPUT, ModbusExt::TEventPriority::HIGH);

    EXPECT_NO_THROW(ev.SendRequests());

    EXPECT_EQ(port.Request.size(), 32);
    EXPECT_EQ(port.Request[0], 0x0A); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x18); // subcommand
    EXPECT_EQ(port.Request[3], 0x1A); // settings size

    EXPECT_EQ(port.Request[4], 0x01); // event type
    EXPECT_EQ(port.Request[5], 0x00); // address MSB
    EXPECT_EQ(port.Request[6], 0x65); // address LSB
    EXPECT_EQ(port.Request[7], 0x02); // count
    EXPECT_EQ(port.Request[8], 0x02); // priority 101
    EXPECT_EQ(port.Request[9], 0x02); // priority 102

    EXPECT_EQ(port.Request[10], 0x04); // event type
    EXPECT_EQ(port.Request[11], 0x00); // address MSB
    EXPECT_EQ(port.Request[12], 0x67); // address LSB
    EXPECT_EQ(port.Request[13], 0x0A); // count
    EXPECT_EQ(port.Request[14], 0x01); // priority 103
    EXPECT_EQ(port.Request[15], 0x01); // priority 104
    EXPECT_EQ(port.Request[16], 0x00); // priority 105
    EXPECT_EQ(port.Request[17], 0x00); // priority 106
    EXPECT_EQ(port.Request[18], 0x01); // priority 107
    EXPECT_EQ(port.Request[19], 0x01); // priority 108
    EXPECT_EQ(port.Request[20], 0x01); // priority 109
    EXPECT_EQ(port.Request[21], 0x02); // priority 110
    EXPECT_EQ(port.Request[22], 0x02); // priority 111
    EXPECT_EQ(port.Request[23], 0x02); // priority 112

    EXPECT_EQ(port.Request[24], 0x04); // event type
    EXPECT_EQ(port.Request[25], 0x00); // address MSB
    EXPECT_EQ(port.Request[26], 0x76); // address LSB
    EXPECT_EQ(port.Request[27], 0x02); // count
    EXPECT_EQ(port.Request[28], 0x02); // priority 118
    EXPECT_EQ(port.Request[29], 0x02); // priority 119

    EXPECT_EQ(port.Request[30], 0x81); // CRC16 LSB
    EXPECT_EQ(port.Request[31], 0x54); // CRC16 MSB

    EXPECT_EQ(response.size(), 14);
    EXPECT_TRUE(response[101]);
    EXPECT_TRUE(response[102]);
    EXPECT_TRUE(response[103]);
    EXPECT_TRUE(response[104]);
    EXPECT_FALSE(response[105]);
    EXPECT_FALSE(response[106]);
    EXPECT_TRUE(response[107]);
    EXPECT_TRUE(response[108]);
    EXPECT_TRUE(response[109]);
    EXPECT_TRUE(response[110]);
    EXPECT_FALSE(response[111]);
    EXPECT_TRUE(response[112]);
    EXPECT_EQ(response.count(113), 0);
    EXPECT_EQ(response.count(114), 0);
    EXPECT_EQ(response.count(115), 0);
    EXPECT_EQ(response.count(116), 0);
    EXPECT_EQ(response.count(117), 0);
    EXPECT_TRUE(response[118]);
    EXPECT_TRUE(response[119]);
}

TEST(TModbusExtTest, ReadEventsNoEventsNoConfirmation)
{
    TPortMock port;
    TTestEventsVisitor visitor;
    ModbusExt::TEventConfirmationState state;

    port.Response = {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x12, 0x52, 0x5D}; // No events
    bool ret = true;
    EXPECT_NO_THROW(ret = ModbusExt::ReadEvents(port, std::chrono::milliseconds(100), 0, state, visitor));
    EXPECT_FALSE(ret);

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // broadcast
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x00); // min slave id
    EXPECT_EQ(port.Request[4], 0xF8); // max length
    EXPECT_EQ(port.Request[5], 0x00); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x00); // flag (confirmation)

    EXPECT_EQ(port.Request[7], 0x79); // CRC16 LSB
    EXPECT_EQ(port.Request[8], 0x5B); // CRC16 MSB

    EXPECT_EQ(visitor.Events.size(), 0);
}

TEST(TModbusExtTest, ReadEventsWithConfirmation)
{
    TPortMock port;
    TTestEventsVisitor visitor;
    ModbusExt::TEventConfirmationState state;

    port.Response =
        {0xFF, 0xFF, 0xFF, 0x05, 0x46, 0x11, 0x01, 0x01, 0x06, 0x02, 0x04, 0x01, 0xD0, 0x04, 0x00, 0x2B, 0xAC};
    bool ret = false;
    EXPECT_NO_THROW(ret = ModbusExt::ReadEvents(port, std::chrono::milliseconds(100), 0, state, visitor));
    EXPECT_TRUE(ret);

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x00); // min slave id
    EXPECT_EQ(port.Request[4], 0xF8); // max length
    EXPECT_EQ(port.Request[5], 0x00); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x00); // flag (confirmation)

    EXPECT_EQ(port.Request[7], 0x79); // CRC16 LSB
    EXPECT_EQ(port.Request[8], 0x5B); // CRC16 MSB

    EXPECT_EQ(visitor.Events.size(), 1);
    EXPECT_EQ(visitor.Events[464], 4);

    port.Response = {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x12, 0x52, 0x5D}; // No events
    visitor.Events.clear();
    EXPECT_NO_THROW(ret = ModbusExt::ReadEvents(port, std::chrono::milliseconds(5), 5, state, visitor));
    EXPECT_FALSE(ret);

    EXPECT_EQ(port.Request.size(), 9);
    EXPECT_EQ(port.Request[0], 0xFD); // slave id
    EXPECT_EQ(port.Request[1], 0x46); // command
    EXPECT_EQ(port.Request[2], 0x10); // subcommand
    EXPECT_EQ(port.Request[3], 0x05); // min slave id
    EXPECT_EQ(port.Request[4], 0x2C); // max length
    EXPECT_EQ(port.Request[5], 0x05); // slave id (confirmation)
    EXPECT_EQ(port.Request[6], 0x01); // flag (confirmation)

    EXPECT_EQ(port.Request[7], 0xFB); // CRC16 LSB
    EXPECT_EQ(port.Request[8], 0x3F); // CRC16 MSB

    EXPECT_EQ(visitor.Events.size(), 0);
}

TEST(TModbusExtTest, ReadEventsReboot)
{
    TPortMock port;
    TTestEventsVisitor visitor;
    ModbusExt::TEventConfirmationState state;

    port.Response =
        {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x11, 0x00, 0x00, 0x04, 0x00, 0x0F, 0x00, 0x00, 0xFF, 0x5E}; // Reboot event
    bool ret = false;
    EXPECT_NO_THROW(ret = ModbusExt::ReadEvents(port, std::chrono::milliseconds(100), 0, state, visitor));
    EXPECT_TRUE(ret);

    EXPECT_TRUE(visitor.Reboot);
}

TEST(TModbusExtTest, GetPacketStart)
{
    uint8_t goodPacket[] = {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x11, 0x00, 0x00, 0x04, 0x00, 0x0F, 0x00, 0x00, 0xFF, 0x5E};
    uint8_t goodPacketWithGlitch[] = {0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFD, 0x46, 0x12, 0x52, 0x5D};
    EXPECT_EQ(ModbusExt::GetPacketStart(goodPacket, sizeof(goodPacket)), goodPacket + 3);
    EXPECT_EQ(ModbusExt::GetPacketStart(goodPacketWithGlitch, sizeof(goodPacketWithGlitch)), goodPacketWithGlitch + 5);

    uint8_t notEnoughDataPacket[] = {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x11, 0x00, 0x00, 0x04, 0x00, 0x0F, 0x00};
    uint8_t badCrcPacket[] = {0xFF, 0xFF, 0xFF, 0xFD, 0x46, 0x11, 0x00, 0x00, 0x04, 0x00, 0x0F, 0x00, 0x00, 0x11, 0x5E};
    EXPECT_EQ(ModbusExt::GetPacketStart(notEnoughDataPacket, sizeof(notEnoughDataPacket)), nullptr);
    EXPECT_EQ(ModbusExt::GetPacketStart(badCrcPacket, sizeof(badCrcPacket)), nullptr);
}

class TModbusExtTraitsTest: public testing::Test
{
public:
    void TestEqual(const std::vector<uint8_t>& b1, const std::vector<uint8_t>& b2) const
    {
        ASSERT_EQ(b1.size(), b2.size());

        for (size_t i = 0; i < b1.size(); ++i) {
            ASSERT_EQ(b1[i], b2[i]) << i;
        }
    }
};

TEST_F(TModbusExtTraitsTest, PacketSize)
{
    ModbusExt::TModbusTraits traits;
    ASSERT_EQ(traits.GetPacketSize(10), 19);
}

TEST_F(TModbusExtTraitsTest, GetPDU)
{
    ModbusExt::TModbusTraits traits;

    const Modbus::TRequest r = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    const Modbus::TRequest r2 = {110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124};

    ASSERT_EQ(*traits.GetPDU(r), 7);
    ASSERT_EQ(*traits.GetPDU(r2), 117);
}

TEST_F(TModbusExtTraitsTest, FinalizeRequest)
{
    ModbusExt::TModbusTraits traits;

    Modbus::TRequest r = {0, 1, 2, 3, 4, 5, 6, 0x06, 0x00, 0x80, 0x00, 0x02, 12, 13};
    Modbus::TRequest p = {0xfd, 0x46, 0x08, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x4d, 0x2a};
    traits.FinalizeRequest(r, 100, 0xfecae7e5);

    TestEqual(r, p);
}

TEST_F(TModbusExtTraitsTest, ReadFrameGood)
{
    TPortMock port;
    port.Response = {0xfd, 0x46, 0x09, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x1c, 0xef};
    ModbusExt::TModbusTraits traits;
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0xfd, 0x46, 0x08, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x4d, 0x2a};
    Modbus::TRequest resp(port.Response.size());

    ASSERT_EQ(traits.ReadFrame(port, t, t, req, resp).Count, 5);

    TestEqual(resp, port.Response);
}

TEST_F(TModbusExtTraitsTest, ReadFrameTooSmallError)
{
    TPortMock port;
    port.Response = {0xfd, 0x46, 0x09, 0xfe, 0xca};
    ModbusExt::TModbusTraits traits;
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0xfd, 0x46, 0x08, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x4d, 0x2a};
    Modbus::TRequest resp(port.Response.size());

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), Modbus::TMalformedResponseError);
}

TEST_F(TModbusExtTraitsTest, ReadFrameInvalidCrc)
{
    TPortMock port;
    port.Response = {0xfd, 0x46, 0x09, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x10, 0xef};
    ModbusExt::TModbusTraits traits;
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0xfd, 0x46, 0x08, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x4d, 0x2a};
    Modbus::TRequest resp(port.Response.size());

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), Modbus::TInvalidCRCError);
}

TEST_F(TModbusExtTraitsTest, ReadFrameInvalidHeader)
{
    TPortMock port;
    port.Response = {0xfe, 0x46, 0x09, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x1c, 0xef};
    SetCrc(port.Response);

    ModbusExt::TModbusTraits traits;
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0xfd, 0x46, 0x08, 0xfe, 0xca, 0xe7, 0xe5, 0x06, 0x00, 0x80, 0x00, 0x02, 0x4d, 0x2a};
    Modbus::TRequest resp(port.Response.size());

    ASSERT_THROW(
        {
            try {
                traits.ReadFrame(port, t, t, req, resp);
            } catch (const TSerialDeviceTransientErrorException& e) {
                EXPECT_STREQ("Serial protocol error: invalid response address", e.what());
                throw;
            }
        },
        TSerialDeviceTransientErrorException);

    port.Response[0] = 0xfd;
    port.Response[1] = 0x45;
    SetCrc(port.Response);
    ASSERT_THROW(
        {
            try {
                traits.ReadFrame(port, t, t, req, resp);
            } catch (const TSerialDeviceTransientErrorException& e) {
                EXPECT_STREQ("Serial protocol error: invalid response command", e.what());
                throw;
            }
        },
        TSerialDeviceTransientErrorException);

    port.Response[1] = 0x46;
    port.Response[2] = 0x10;
    SetCrc(port.Response);
    ASSERT_THROW(
        {
            try {
                traits.ReadFrame(port, t, t, req, resp);
            } catch (const TSerialDeviceTransientErrorException& e) {
                EXPECT_STREQ("Serial protocol error: invalid response subcommand", e.what());
                throw;
            }
        },
        TSerialDeviceTransientErrorException);

    port.Response[2] = 0x09;
    port.Response[3] = 0x01;
    SetCrc(port.Response);
    ASSERT_THROW(
        {
            try {
                traits.ReadFrame(port, t, t, req, resp);
            } catch (const TSerialDeviceTransientErrorException& e) {
                EXPECT_STREQ("Serial protocol error: SN mismatch", e.what());
                throw;
            }
        },
        TSerialDeviceTransientErrorException);
}
