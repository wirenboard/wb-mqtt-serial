#include <gtest/gtest.h>

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
