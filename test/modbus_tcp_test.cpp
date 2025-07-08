#include "fake_serial_port.h"
#include "modbus_common.h"

namespace
{
    class TPortMock: public TPort
    {
        size_t Pointer = 0;
        std::vector<uint8_t> Stream;
        std::vector<uint8_t> LastRequest;

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
        {
            LastRequest.assign(buf, buf + count);
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

        const std::vector<uint8_t>& GetLastRequest() const
        {
            return LastRequest;
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

TEST_F(TModbusTCPTraitsTest, ReadFrameGood)
{
    std::vector<uint8_t> r = {0, 1, 0, 0, 0, 3, 100, 17, 18};
    TPortMock port(r);
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    auto resp = traits.Transaction(port, 100, req, 2, t, t).Pdu;
    ASSERT_EQ(resp.size(), 2);
    TestEqual(resp, {17, 18});
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallMBAP)
{
    TPortMock port({0, 1, 0, 0});
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    ASSERT_THROW(traits.Transaction(port, 100, req, 2, t, t), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallMBAPLength)
{
    TPortMock port({0, 1, 0, 0, 0, 0, 100, 7, 8, 9});
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    ASSERT_THROW(traits.Transaction(port, 100, req, 2, t, t), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameSmallPDU)
{
    TPortMock port({0, 1, 0, 0, 0, 4, 100, 7});
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    ASSERT_THROW(traits.Transaction(port, 100, req, 2, t, t), Modbus::TMalformedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFrameWrongUnitId)
{
    TPortMock port({0, 1, 0, 0, 0, 4, 101, 7, 8, 9});
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    ASSERT_THROW(traits.Transaction(port, 100, req, 2, t, t), Modbus::TUnexpectedResponseError);
}

TEST_F(TModbusTCPTraitsTest, ReadFramePassWrongTransactionId)
{
    std::vector<uint8_t> goodResp = {0, 1, 0, 0, 0, 4, 100, 17, 18, 19};
    std::vector<uint8_t> r = {0, 2, 0, 0, 0, 8, 101, 7, 8, 9, 10, 11, 12, 13};
    r.insert(r.end(), goodResp.begin(), goodResp.end());
    TPortMock port(r);

    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    TestEqual(traits.Transaction(port, 100, req, 2, t, t).Pdu, {17, 18, 19});
}

TEST_F(TModbusTCPTraitsTest, ReadFrameTimeout)
{
    TPortMock port({0, 2, 0, 0, 0, 4, 101, 7, 8, 9});
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    ASSERT_THROW(traits.Transaction(port, 100, req, 2, t, t), TResponseTimeoutException);
}

TEST_F(TModbusTCPTraitsTest, IncrementTransactionId)
{
    std::vector<uint8_t> r = {0, 1, 0, 0, 0, 3, 100, 17, 18, 0, 2, 0, 0, 0, 3, 100, 19, 20};
    TPortMock port(r);
    Modbus::TModbusTCPTraits::ResetTransactionId(port);
    Modbus::TModbusTCPTraits traits;
    std::chrono::milliseconds t(10);

    std::vector<uint8_t> req = {7, 8, 9};
    auto resp = traits.Transaction(port, 100, req, 2, t, t).Pdu;
    ASSERT_EQ(resp.size(), 2);
    TestEqual(resp, {17, 18});
    ASSERT_EQ(port.GetLastRequest()[0], 0);
    ASSERT_EQ(port.GetLastRequest()[1], 1);

    Modbus::TModbusTCPTraits traits2;
    req.assign({10, 11, 12});
    resp = traits2.Transaction(port, 100, req, 2, t, t).Pdu;
    ASSERT_EQ(resp.size(), 2);
    TestEqual(resp, {19, 20});
    ASSERT_EQ(port.GetLastRequest()[0], 0);
    ASSERT_EQ(port.GetLastRequest()[1], 2);
}
