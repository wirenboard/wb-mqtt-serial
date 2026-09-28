#include <gtest/gtest.h>

#include "rpc/rpc_exception.h"
#include "rpc/rpc_port_settings.h"

TEST(TRPCPortSettingsTest, ParseSerialPort)
{
    Json::Value request;
    request["path"] = "/dev/ttyRS485-1";
    request["baud_rate"] = 115200;
    request["parity"] = "E";
    request["stop_bits"] = 2;

    auto port = ParseRPCPort(request, false);
    const auto& settings = std::get<TSerialPortSettings>(port);
    ASSERT_EQ(settings.Device, "/dev/ttyRS485-1");
    ASSERT_EQ(settings.BaudRate, 115200);
    ASSERT_EQ(settings.Parity, 'E');
    ASSERT_EQ(settings.StopBits, 2);
    ASSERT_EQ(GetRPCPortDescription(port), "/dev/ttyRS485-1");
}

TEST(TRPCPortSettingsTest, ParseTcpPort)
{
    Json::Value request;
    request["ip"] = "192.168.1.10";
    request["port"] = 23;

    auto port = ParseRPCPort(request, false);
    const auto& settings = std::get<TRPCTcpPortSettings>(port);
    ASSERT_EQ(settings.Address, "192.168.1.10");
    ASSERT_EQ(settings.Port, 23);
    ASSERT_FALSE(settings.ModbusTcp);
    ASSERT_EQ(GetRPCPortDescription(port), "192.168.1.10:23");
}

//! Modbus TCP is spoken only over a TCP port, so the flag belongs to it
TEST(TRPCPortSettingsTest, ParseModbusTcpPort)
{
    Json::Value request;
    request["ip"] = "192.168.1.10";
    request["port"] = 502;

    auto port = ParseRPCPort(request, true);
    ASSERT_TRUE(std::get<TRPCTcpPortSettings>(port).ModbusTcp);
}

TEST(TRPCPortSettingsTest, ParseTcpPortWithoutPortNumber)
{
    Json::Value request;
    request["address"] = "192.168.1.100";

    ASSERT_THROW(ParseRPCPort(request, false), TRPCException);
}

TEST(TRPCPortSettingsTest, ParseUndefinedPort)
{
    ASSERT_THROW(ParseRPCPort(Json::Value(), false), TRPCException);
}
