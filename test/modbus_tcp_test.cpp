#include "fake_serial_port.h"
#include "modbus_common.h"

namespace
{
    class TPortMock: public TPort
    {
        size_t Pointer = 0;
        std::vector<uint8_t> Stream;

    public:
        TPortMock(const std::vector<uint8_t>& stream): Stream(stream)
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
        {}

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
            TReadFrameResult res;
            if (Pointer == Stream.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                Pointer = 0;
            }
            res.Count = std::min(count, Stream.size() - Pointer);
            memcpy(buf, &Stream[Pointer], res.Count);
            Pointer += res.Count;
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
    };
}

class TModbusTCPTraitsTest: public testing::Test
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

TEST_F(TModbusTCPTraitsTest, PacketSize)
{
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    ASSERT_EQ(traits.GetPacketSize(10), 17); // Packet size == PDU size + MBAP size (7 bytes)
}

TEST_F(TModbusTCPTraitsTest, GetPDU)
{
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));

    Modbus::TRequest r = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const Modbus::TRequest r2 = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};

    ASSERT_EQ(*traits.GetPDU(r), 7);
    ASSERT_EQ(*traits.GetPDU(r2), 17);
}

TEST_F(TModbusTCPTraitsTest, FinalizeRequest)
{
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));

    Modbus::TRequest r = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    Modbus::TRequest p = {0, 11, 0, 0, 0, 4, 100, 7, 8, 9};
    traits.FinalizeRequest(r, 100, 0);

    Modbus::TRequest r2 = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    Modbus::TRequest p2 = {0, 12, 0, 0, 0, 5, 200, 17, 18, 19, 20};
    traits.FinalizeRequest(r2, 200, 0);

    TestEqual(r, p);
    TestEqual(r2, p2);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameGood)
{
    std::vector<uint8_t> r = {0, 1, 0, 0, 0, 2, 100, 17};
    TPortMock port(r);
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_EQ(traits.ReadFrame(port, t, t, req, resp).Count, 1);

    TestEqual(resp, r);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallMBAP)
{
    TPortMock port({0, 1, 0, 0});
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallMBAPLength)
{
    TPortMock port({0, 1, 0, 0, 0, 0, 100, 7, 8, 9});
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallPDU)
{
    TPortMock port({0, 1, 0, 0, 0, 4, 100, 7});
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameWrongUnitId)
{
    TPortMock port({0, 1, 0, 0, 0, 4, 101, 7, 8, 9});
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), TSerialDeviceTransientErrorException);
}

TEST_F(TModbusTCPTraitsTest, ReadFramePassWrongTransactionId)
{
    Modbus::TResponse goodResp = {0, 1, 0, 0, 0, 4, 100, 17, 18, 19};
    std::vector<uint8_t> r = {0, 2, 0, 0, 0, 8, 101, 7, 8, 9, 10, 11, 12, 13};
    r.insert(r.end(), goodResp.begin(), goodResp.end());
    TPortMock port(r);

    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    auto pduSize = traits.ReadFrame(port, t, t, req, resp).Count;
    resp.resize(traits.GetPacketSize(pduSize));
    TestEqual(resp, goodResp);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameTimeout)
{
    TPortMock port({0, 2, 0, 0, 0, 4, 101, 7, 8, 9});
    Modbus::TModbusTCPTraits traits(std::make_shared<uint16_t>(10));
    std::chrono::milliseconds t(10);

    Modbus::TRequest req = {0, 1, 0, 0, 0, 4, 100, 7, 8, 9};
    Modbus::TRequest resp;

    ASSERT_THROW(traits.ReadFrame(port, t, t, req, resp), TSerialDeviceTransientErrorException);
}
