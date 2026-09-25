#include <gtest/gtest.h>

#include "port/serial_port.h"
#include "port/tcp_port.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_port_driver_list.h"

/**
 * The service keeps serving RPC when the configuration has failed to load, then it has no driver.
 * A request addressing a device by its identifier must be answered with an error and not crash
 * the service
 */
TEST(TSerialClientTaskRunnerTest, RequestByDeviceIdWithoutDriver)
{
    TSerialClientTaskRunner taskRunner(nullptr);

    Json::Value request;
    request["device_id"] = "wb-mr6cv3_25";
    ASSERT_THROW(taskRunner.GetSerialClientParams(request), TRPCException);
}

//! A request by port parameters is served without a driver, the port is opened by the task executor
TEST(TSerialClientTaskRunnerTest, RequestByPortParamsWithoutDriver)
{
    TSerialClientTaskRunner taskRunner(nullptr);

    Json::Value request;
    request["path"] = "/dev/ttyRS485-1";
    auto params = taskRunner.GetSerialClientParams(request);
    ASSERT_FALSE(params.SerialClient);
    ASSERT_FALSE(params.Device);
}

//! A request without any addressing is rejected, with or without a driver
TEST(TSerialClientTaskRunnerTest, RequestWithoutPortIsRejected)
{
    TSerialClientTaskRunner taskRunner(nullptr);

    ASSERT_THROW(taskRunner.GetSerialClientParams(Json::Value()), TRPCException);
}

//! A request names the polled port by fields, a serial path never matches a TCP port
TEST(TPortMatchTest, SerialPort)
{
    TFeaturePort port(std::make_shared<TSerialPort>(TSerialPortSettings("/dev/ttyRS485-1")), false);

    Json::Value request;
    request["path"] = "/dev/ttyRS485-1";
    ASSERT_TRUE(PortMatches(ParseRPCPort(request, false), port));

    request["path"] = "/dev/ttyRS485-2";
    ASSERT_FALSE(PortMatches(ParseRPCPort(request, false), port));
}

TEST(TPortMatchTest, TcpPort)
{
    TFeaturePort port(std::make_shared<TTcpPort>(TTcpPortSettings("192.168.1.10", 23)), false);

    Json::Value request;
    request["ip"] = "192.168.1.10";
    request["port"] = 23;
    ASSERT_TRUE(PortMatches(ParseRPCPort(request, false), port));

    request["port"] = 24;
    ASSERT_FALSE(PortMatches(ParseRPCPort(request, false), port));
}

TEST(TPortMatchTest, TcpPortIsNotNamedByItsDescription)
{
    TFeaturePort port(std::make_shared<TTcpPort>(TTcpPortSettings("192.168.1.10", 23)), false);

    Json::Value request;
    request["path"] = "192.168.1.10:23";
    ASSERT_FALSE(PortMatches(ParseRPCPort(request, false), port));
}
