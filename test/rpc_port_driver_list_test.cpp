#include <gtest/gtest.h>

#include "port/serial_port.h"
#include "port/tcp_port.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_port_driver_list.h"
#include "test_utils.h"

#include <wblib/testing/testlog.h>

namespace
{
    class TNoopTask: public ISerialClientTask
    {
    public:
        ISerialClientTask::TRunResult Run(PFeaturePort port,
                                          TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                          const std::list<PSerialDevice>& polledDevices) override
        {
            return ISerialClientTask::TRunResult::OK;
        }
    };
}

//! The service keeps serving RPC when the configuration has failed to load, then it has no driver
class TSerialClientTaskRunnerTest: public testing::Test
{
protected:
    void SetUp() override
    {
        ProtocolSchemas = std::make_unique<TProtocolConfedSchemasMap>(
            WBMQTT::Testing::TLoggedFixture::GetDataFilePath("../protocols"),
            CommonDeviceSchema);
        ConfigLoader = std::make_unique<TConfigLoader>(std::string(),
                                                       DeviceFactory,
                                                       CommonDeviceSchema,
                                                       Templates,
                                                       PortsSchema,
                                                       *ProtocolSchemas);
        SerialDriverCore =
            std::make_unique<TSerialDriverCore>(MakeUnconnectedMqttDriver(), nullptr, *ConfigLoader, false);
        TaskRunner = std::make_unique<TSerialClientTaskRunner>(*SerialDriverCore);
    }

    TSerialDeviceFactory DeviceFactory;
    Json::Value CommonDeviceSchema;
    TTemplateMap Templates;
    Json::Value PortsSchema;
    std::unique_ptr<TProtocolConfedSchemasMap> ProtocolSchemas;
    std::unique_ptr<TConfigLoader> ConfigLoader;
    std::unique_ptr<TSerialDriverCore> SerialDriverCore;
    std::unique_ptr<TSerialClientTaskRunner> TaskRunner;
};

//! A request addressing a device by its identifier must be answered with an error and not crash the service
TEST_F(TSerialClientTaskRunnerTest, RequestByDeviceIdWithoutDriver)
{
    Json::Value request;
    request["device_id"] = "wb-mr6cv3_25";
    ASSERT_THROW(TaskRunner->RunTask(request,
                                     [](PSerialDevice, PDeviceParametersCache) -> PSerialClientTask {
                                         return std::make_shared<TNoopTask>();
                                     }),
                 TRPCException);
    ASSERT_THROW(TaskRunner->SetPoll(request, false), TRPCException);
}

//! A request by port parameters is served without a driver, the port is opened by the task executor
TEST_F(TSerialClientTaskRunnerTest, RequestByPortParamsWithoutDriver)
{
    Json::Value request;
    request["path"] = "/dev/ttyRS485-1";
    bool taskMade = false;
    TaskRunner->RunTask(request, [&](PSerialDevice device, PDeviceParametersCache) -> PSerialClientTask {
        taskMade = true;
        EXPECT_FALSE(device);
        return std::make_shared<TNoopTask>();
    });
    ASSERT_TRUE(taskMade);
    ASSERT_THROW(TaskRunner->SetPoll(request, false), TRPCException);
}

//! A request without any addressing is rejected, with or without a driver
TEST_F(TSerialClientTaskRunnerTest, RequestWithoutPortIsRejected)
{
    ASSERT_THROW(TaskRunner->RunTask(Json::Value(),
                                     [](PSerialDevice, PDeviceParametersCache) -> PSerialClientTask {
                                         return std::make_shared<TNoopTask>();
                                     }),
                 TRPCException);
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
