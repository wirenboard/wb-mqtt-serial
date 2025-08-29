#include "tcp_port.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT::Testing;

class TTcpPortTest: public TLoggedFixture
{};

TEST_F(TTcpPortTest, TestGetSendTimeBytes)
{
    TTcpPortSettings settings;
    TTcpPort port(settings);
    EXPECT_EQ(port.GetSendTimeBytes(0), std::chrono::microseconds(0));
    EXPECT_EQ(port.GetSendTimeBytes(1), std::chrono::microseconds(1146));
    EXPECT_EQ(port.GetSendTimeBytes(10), std::chrono::microseconds(11459));
}

TEST_F(TTcpPortTest, TestGetSendTimeBits)
{
    TTcpPortSettings settings;
    TTcpPort port(settings);
    EXPECT_EQ(port.GetSendTimeBits(0), std::chrono::microseconds(0));
    EXPECT_EQ(port.GetSendTimeBits(1), std::chrono::microseconds(105));
    EXPECT_EQ(port.GetSendTimeBits(10), std::chrono::microseconds(1042));
}
