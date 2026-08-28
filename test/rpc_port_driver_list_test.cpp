#include <gtest/gtest.h>

#include "rpc/rpc_exception.h"
#include "rpc/rpc_port_driver_list.h"

// Config may fail to load, then there is no serial driver, but RPC requests are still served

TEST(TSerialClientTaskRunnerTest, DeviceIdRequestWithoutSerialDriver)
{
    TSerialClientTaskRunner taskRunner(nullptr);

    Json::Value request;
    request["device_id"] = "wb-mr6c_1";
    request["path"] = "/dev/ttyRS485-1";

    auto params = taskRunner.GetSerialClientParams(request);
    ASSERT_EQ(nullptr, params.SerialClient);
    ASSERT_EQ(nullptr, params.Device);
}

TEST(TSerialClientTaskRunnerTest, DeviceIdRequestWithoutPortAndSerialDriver)
{
    TSerialClientTaskRunner taskRunner(nullptr);

    Json::Value request;
    request["device_id"] = "wb-mr6c_1";

    ASSERT_THROW(taskRunner.GetSerialClientParams(request), TRPCException);
}
