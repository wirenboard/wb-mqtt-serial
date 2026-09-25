#include <gtest/gtest.h>
#include <wblib/testing/testlog.h>

#include "fake_serial_port.h"
#include "port/feature_port.h"
#include "rpc/rpc_port_scan_serial_client_task.h"
#include "rpc/rpc_port_setup_serial_client_task.h"

class TRPCPortFastModbusDisabledTest: public WBMQTT::Testing::TLoggedFixture
{};

//! The fake port has no expectations, any exchange fails, the port stays closed if the task makes no exchange
TEST_F(TRPCPortFastModbusDisabledTest, ScanReturnsEmptyList)
{
    auto port = std::make_shared<TFakeSerialPort>(*this, "<fast_modbus_disabled>");
    Json::Value request;
    request["mode"] = "start";
    Json::Value result;
    std::string error;
    TRPCPortScanSerialClientTask task(
        request,
        [&](const Json::Value& reply) { result = reply; },
        [&](auto, const std::string& message) { error = message; });
    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    task.Run(std::make_shared<TFeaturePort>(port, false, false, true), accessHandler, {});

    EXPECT_TRUE(error.empty()) << error;
    ASSERT_TRUE(result["devices"].isArray());
    EXPECT_EQ(result["devices"].size(), 0);
    EXPECT_FALSE(port->IsOpen());
}

namespace
{
    std::string RunSetup(PFeaturePort port, bool withSn, bool& gotResult)
    {
        Json::Value item;
        item["slave_id"] = 1;
        if (withSn) {
            item["sn"] = 12345;
        }
        Json::Value request;
        request["items"].append(item);
        std::string error;
        TRPCPortSetupSerialClientTask task(
            request,
            [&](const Json::Value&) { gotResult = true; },
            [&](auto, const std::string& message) { error = message; });
        TSerialClientDeviceAccessHandler accessHandler(nullptr);
        task.Run(port, accessHandler, {});
        return error;
    }
}

TEST_F(TRPCPortFastModbusDisabledTest, SetupWithSnIsRejected)
{
    auto port = std::make_shared<TFakeSerialPort>(*this, "<fast_modbus_disabled>");
    bool gotResult = false;
    auto error = RunSetup(std::make_shared<TFeaturePort>(port, false, false, true), true, gotResult);

    EXPECT_FALSE(gotResult);
    EXPECT_EQ(error, "Fast Modbus is not available on the port");
    EXPECT_FALSE(port->IsOpen());
}

TEST_F(TRPCPortFastModbusDisabledTest, SetupWithSnOnModbusTcpWithoutMgeIsRejected)
{
    auto port = std::make_shared<TFakeSerialPort>(*this, "<modbus_tcp_without_mge>");
    bool gotResult = false;
    auto error = RunSetup(std::make_shared<TFeaturePort>(port, true, false, false), true, gotResult);

    EXPECT_FALSE(gotResult);
    EXPECT_EQ(error, "Fast Modbus is not available on the port");
    EXPECT_FALSE(port->IsOpen());
}

//! The fake port refuses to open, so the open error shows that the request passed the Fast Modbus check
TEST_F(TRPCPortFastModbusDisabledTest, SetupWithoutSnIsNotRejected)
{
    auto port = std::make_shared<TFakeSerialPort>(*this, "<fast_modbus_disabled>");
    port->SetAllowOpen(false);
    bool gotResult = false;
    auto error = RunSetup(std::make_shared<TFeaturePort>(port, false, false, true), false, gotResult);

    EXPECT_FALSE(gotResult);
    EXPECT_EQ(error, "Port IO error: Serial protocol error: Port open error simulation");
}
