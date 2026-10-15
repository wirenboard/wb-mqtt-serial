#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <thread>

#include <wblib/backend.h>
#include <wblib/driver.h>
#include <wblib/driver_args.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

#include "file_utils.h"
#include "modbus_simulator_port.h"
#include "rpc/rpc_config_change_handler.h"
#include "rpc/rpc_config_handler.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_helpers.h"
#include "rpc_service_test_utils.h"
#include "serial_config.h"
#include "test_utils.h"

using namespace std::chrono_literals;
using WBMQTT::Testing::TLoggedFixture;

namespace
{
    const auto PORT_TASKS_WAIT_TIMEOUT = 300ms;
    const auto WAIT_TIMEOUT = SERVICE_TEST_WAIT_TIMEOUT;

    const std::string PORT1 = "/dev/ttyTemplatesApplyTest1";
    const std::string PORT2 = "/dev/ttyTemplatesApplyTest2";

    //! Has a stock template, the tests upload a user one
    const std::string TYPE = "apply-device";
    //! Has only a user template
    const std::string USER_TYPE = "apply-user-device";
    const std::string TEMPLATE_FILE = "apply-device.json";

    //! The model of the devices of the port, the templates of the tests have it in "hw"
    const std::string MODEL = "WBAPPLY";
    const uint16_t MODEL_ADDRESS = 200;
    const uint16_t FW_ADDRESS = 250;

    const std::string APPLY_PROBLEMS_PREFIX = "apply-problems: ";

    //! The values of the configuration made by MakeStockDevice
    const std::vector<TModbusSimulatorPort::TWrite> STOCK_DEVICE_SETUP = {{1, 10, 5}, {1, 11, 6}};

    Json::Value MakeParameter(const std::string& id, int address, int min = -1, int max = -1)
    {
        Json::Value parameter;
        parameter["id"] = id;
        parameter["title"] = id;
        parameter["reg_type"] = "holding";
        parameter["address"] = address;
        if (min >= 0) {
            parameter["min"] = min;
            parameter["max"] = max;
        }
        return parameter;
    }

    Json::Value MakeTemplateWithoutHw(const std::string& deviceType, const std::vector<Json::Value>& parameters)
    {
        Json::Value root;
        root["device_type"] = deviceType;
        root["title"] = deviceType;
        root["device"]["name"] = deviceType;
        root["device"]["id"] = deviceType;
        Json::Value channel;
        channel["name"] = "Value";
        channel["reg_type"] = "holding";
        channel["address"] = 0;
        root["device"]["channels"].append(channel);
        root["device"]["parameters"] = Json::Value(Json::arrayValue);
        for (const auto& parameter: parameters) {
            root["device"]["parameters"].append(parameter);
        }
        return root;
    }

    //! The settings of its devices are read
    Json::Value MakeTemplate(const std::string& deviceType, const std::vector<Json::Value>& parameters)
    {
        auto root = MakeTemplateWithoutHw(deviceType, parameters);
        Json::Value hw;
        hw["signature"] = MODEL;
        root["hw"].append(hw);
        return root;
    }

    //! The stock template: p1 and p2
    Json::Value MakeStockTemplate()
    {
        return MakeTemplate(TYPE, {MakeParameter("p1", 10, 0, 100), MakeParameter("p2", 11)});
    }

    //! The uploaded template: p1 is another register, p2 is gone, p3 is new
    Json::Value MakeNewTemplate()
    {
        return MakeTemplate(TYPE, {MakeParameter("p1", 20, 0, 100), MakeParameter("p3", 100)});
    }

    Json::Value MakeDevice(const std::string& deviceType, int slaveId, const std::string& id)
    {
        Json::Value device;
        device["device_type"] = deviceType;
        device["slave_id"] = std::to_string(slaveId);
        device["id"] = id;
        return device;
    }

    //! The device of the stock template with the parameters set in the configuration
    Json::Value MakeStockDevice(int slaveId)
    {
        auto device = MakeDevice(TYPE, slaveId, TYPE + "-" + std::to_string(slaveId));
        device["p1"] = 5;
        device["p2"] = 6;
        return device;
    }

    Json::Value MakePort(const std::string& path, const std::vector<Json::Value>& devices)
    {
        Json::Value port;
        port["path"] = path;
        port["devices"] = Json::Value(Json::arrayValue);
        for (const auto& device: devices) {
            port["devices"].append(device);
        }
        return port;
    }

    Json::Value MakeConfig(const std::vector<Json::Value>& ports)
    {
        Json::Value config;
        config["ports"] = Json::Value(Json::arrayValue);
        for (const auto& port: ports) {
            config["ports"].append(port);
        }
        return config;
    }

    Json::Value MakeProblemDevices(const std::vector<std::pair<std::string, int>>& devices)
    {
        Json::Value res(Json::arrayValue);
        for (const auto& device: devices) {
            Json::Value item;
            item["port"] = device.first;
            item["slave_id"] = std::to_string(device.second);
            res.append(std::move(item));
        }
        return res;
    }

    //! The problems by the port and the slave id of the devices
    Json::Value MakeProblems(const std::vector<std::pair<std::string, int>>& disabled,
                             const std::vector<std::pair<std::string, int>>& unread,
                             const std::vector<std::pair<std::string, int>>& invalid)
    {
        Json::Value res;
        res["disabled_devices"] = MakeProblemDevices(disabled);
        res["unread_devices"] = MakeProblemDevices(unread);
        res["invalid_devices"] = MakeProblemDevices(invalid);
        return res;
    }

    std::string ReadFile(const std::string& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    //! The files watcher may reload the template, so the template is compared by its file and content
    std::string DescribeTemplate(const PDeviceTemplate& deviceTemplate)
    {
        return deviceTemplate->GetFilePath() + "\n" + SerializeJson(deviceTemplate->GetTemplate());
    }

    //! The names and the contents of the files
    std::map<std::string, std::string> ReadDir(const std::filesystem::path& dir)
    {
        std::map<std::string, std::string> res;
        for (const auto& entry: std::filesystem::directory_iterator(dir)) {
            res[entry.path().filename().string()] =
                entry.is_regular_file() ? ReadFile(entry.path().string()) : std::string("<dir>");
        }
        return res;
    }
}

/**
 * The service as main() assembles it with the ports of TModbusSimulatorPort.
 * The MQTT log is not compared with a file, the tests assert the behaviour themselves
 */
class TRPCTemplatesApplyTest: public TLoggedFixture
{
protected:
    void SetUp() override;
    void TearDown() override;

    //! The configuration the service starts with
    virtual Json::Value InitialConfig();

    //! The file is named after the type by default
    PAsyncAnswer Upload(const Json::Value& templateRoot, bool force, const std::string& filename = std::string());
    PAsyncAnswer Delete(const std::string& deviceType, bool force);
    //! error.data of a refused config/Save, an empty string on success
    std::string Save(const Json::Value& config);
    //! Saves the configuration and waits for the writes of the setup, by default of the stock device 1
    void SaveAndWaitSetup(const Json::Value& config,
                          const std::vector<TModbusSimulatorPort::TWrite>& writes = STOCK_DEVICE_SETUP);
    bool WaitForWrites(const std::vector<TModbusSimulatorPort::TWrite>& writes);

    std::string ReadConfigFile();
    Json::Value ReadConfigJson();
    PSerialDevice FindPolledDevice(const std::string& deviceType, const std::string& slaveId);
    std::shared_ptr<TBlockingTask> RunBlockingTask(const std::string& deviceId);

    //! Calls the request, checks that it changes nothing and returns the error
    std::string CallChangingNothing(const std::function<PAsyncAnswer()>& request);
    //! Checks that the request is refused with the problems and changes nothing
    void ExpectRefusedWithProblems(const std::function<PAsyncAnswer()>& request, const Json::Value& problems);
    //! Checks that the upload without force is refused with the problems and is applied with it
    void ExpectAppliedWithProblems(const Json::Value& templateRoot, const Json::Value& problems);
    //! Checks that the configuration of the response is the one of the file
    void ExpectConfigInResponse(const PAsyncAnswer& answer);

    std::filesystem::path TestDir;
    std::filesystem::path StockTemplatesDir;
    std::filesystem::path UserTemplatesDir;
    std::string ConfigPath;
    std::chrono::milliseconds PortTasksWaitTimeout = PORT_TASKS_WAIT_TIMEOUT;

    std::map<std::string, PModbusSimulatorPort> Ports;

    Json::Value GroupTranslations;
    PTemplateMap Templates;

    WBMQTT::Testing::PFakeMqttBroker MqttBroker;
    WBMQTT::Testing::PFakeMqttClient MqttClient;
    WBMQTT::PDeviceDriver Driver;

    std::unique_ptr<TSerialDriverCore> Core;
    std::unique_ptr<TSerialClientTaskRunner> TaskRunner;
    std::shared_ptr<TRPCMethods> Rpc;
    std::unique_ptr<TRPCConfigHandler> ConfigHandler;
    std::unique_ptr<TRPCConfigChangeHandler> ConfigChangeHandler;
    std::unique_ptr<TRPCDeviceHandler> DeviceHandler;

    std::vector<std::shared_ptr<TBlockingTask>> BlockingTasks;
};

void TRPCTemplatesApplyTest::SetUp()
{
    TLoggedFixture::SetUp();
    TestDir = std::filesystem::temp_directory_path() / "wb-mqtt-serial-templates-apply-test" /
              ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(TestDir);
    StockTemplatesDir = TestDir / "stock";
    UserTemplatesDir = TestDir / "templates";
    std::filesystem::create_directories(StockTemplatesDir);
    std::filesystem::create_directories(UserTemplatesDir);
    ConfigPath = (TestDir / "wb-mqtt-serial.conf").string();
    WriteToFile((StockTemplatesDir / TEMPLATE_FILE).string(), SerializeJson(MakeStockTemplate()));

    for (const auto& path: {PORT1, PORT2}) {
        Ports[path] = std::make_shared<TModbusSimulatorPort>(path);
    }
    auto& port = *Ports[PORT1];
    port.AddDevice(1);
    port.SetString(1, MODEL_ADDRESS, MODEL);
    port.SetRegister(1, 10, 7);
    port.SetRegister(1, 11, 8);
    port.SetRegister(1, 20, 30);
    port.SetRegister(1, 100, 40);

    GroupTranslations = WBMQTT::JSON::Parse(GetDataFilePath("../groups.json"));

    MqttBroker = WBMQTT::Testing::NewFakeMqttBroker(*this);
    MqttClient = MqttBroker->MakeClient("templates-apply-test");
    Driver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
                                   .SetId("templates-apply-test")
                                   .SetBackend(WBMQTT::NewDriverBackend(MqttClient))
                                   .SetIsTesting(true)
                                   .SetReownUnknownDevices(true)
                                   .SetUseStorage(true)
                                   .SetStoragePath((TestDir / "libwbmqtt.db").string()));
    Driver->StartLoop();

    WriteToFile(ConfigPath, SerializeJson(InitialConfig()));
    Core = std::make_unique<TSerialDriverCore>(
        Driver,
        MakeSerialDriverCoreFiles(ConfigPath, StockTemplatesDir.string(), UserTemplatesDir.string()),
        false,
        [this](const TFeaturePortSettings& settings) {
            return std::make_shared<TFeaturePort>(Ports.at(std::get<TSerialPortSettings>(settings.Port).Device),
                                                  false,
                                                  false,
                                                  true);
        },
        PortTasksWaitTimeout);
    Templates = Core->GetTemplates();
    TaskRunner = std::make_unique<TSerialClientTaskRunner>(*Core);

    Rpc = std::make_shared<TRPCMethods>();
    ConfigHandler = std::make_unique<TRPCConfigHandler>(GroupTranslations, *Core, Rpc);
    ConfigChangeHandler = std::make_unique<TRPCConfigChangeHandler>(
        GroupTranslations,
        GetDataFilePath("../wb-mqtt-serial-rpc-templates-upload-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-templates-delete-request.schema.json"),
        *Core);
    RegisterConfigChangeRpcHandlers(*ConfigChangeHandler, Rpc);
    DeviceHandler = std::make_unique<TRPCDeviceHandler>(
        ConfigPath,
        GetDataFilePath("../wb-mqtt-serial-rpc-device-load-config-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-load-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-set-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-probe-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-set-poll-request.schema.json"),
        Core->GetDeviceFactory(),
        Templates,
        *TaskRunner,
        Rpc);

    Core->Start();
    // The tests check the writes after the setup of the device of the initial configuration
    if (Core->GetConfig()) {
        ASSERT_TRUE(WaitForWrites(STOCK_DEVICE_SETUP));
    }
}

void TRPCTemplatesApplyTest::TearDown()
{
    for (const auto& task: BlockingTasks) {
        task->Release();
    }
    // Waits for the configuration change being called
    Rpc->Stop();
    ConfigHandler.reset();
    ConfigChangeHandler.reset();
    Core->Stop();
    DeviceHandler.reset();
    TaskRunner.reset();
    Templates.reset();
    Core.reset();
    Driver->StopLoop();
    std::filesystem::remove_all(TestDir);
}

Json::Value TRPCTemplatesApplyTest::InitialConfig()
{
    return MakeConfig({MakePort(PORT1, {MakeStockDevice(1)})});
}

PAsyncAnswer TRPCTemplatesApplyTest::Upload(const Json::Value& templateRoot, bool force, const std::string& filename)
{
    Json::Value request;
    request["content"] = SerializeJson(templateRoot);
    request["filename"] = filename.empty() ? templateRoot["device_type"].asString() + ".json" : filename;
    request["force"] = force;
    return CallAsync(*Rpc, "templates/Upload", request);
}

PAsyncAnswer TRPCTemplatesApplyTest::Delete(const std::string& deviceType, bool force)
{
    Json::Value request;
    request["type"] = deviceType;
    request["force"] = force;
    return CallAsync(*Rpc, "templates/Delete", request);
}

std::string TRPCTemplatesApplyTest::Save(const Json::Value& config)
{
    Json::Value request;
    request["config"] = config;
    return CallAsync(*Rpc, "config/Save", request)->Get();
}

void TRPCTemplatesApplyTest::SaveAndWaitSetup(const Json::Value& config,
                                              const std::vector<TModbusSimulatorPort::TWrite>& writes)
{
    Ports[PORT1]->ClearWrites();
    ASSERT_EQ("", Save(config));
    ASSERT_TRUE(WaitForWrites(writes));
}

bool TRPCTemplatesApplyTest::WaitForWrites(const std::vector<TModbusSimulatorPort::TWrite>& writes)
{
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    for (const auto& write: writes) {
        while (true) {
            auto portWrites = Ports[PORT1]->GetWrites();
            if (std::find(portWrites.begin(), portWrites.end(), write) != portWrites.end()) {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(10ms);
        }
    }
    return true;
}

std::string TRPCTemplatesApplyTest::ReadConfigFile()
{
    return ReadFile(ConfigPath);
}

Json::Value TRPCTemplatesApplyTest::ReadConfigJson()
{
    return ParseJson(ReadConfigFile());
}

PSerialDevice TRPCTemplatesApplyTest::FindPolledDevice(const std::string& deviceType, const std::string& slaveId)
{
    auto config = Core->GetConfig();
    if (!config) {
        return nullptr;
    }
    for (const auto& portConfig: config->PortConfigs) {
        for (const auto& device: portConfig->Devices) {
            const auto& deviceConfig = device->Device->DeviceConfig();
            if (deviceConfig->DeviceType == deviceType && deviceConfig->SlaveId == slaveId) {
                return device->Device;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<TBlockingTask> TRPCTemplatesApplyTest::RunBlockingTask(const std::string& deviceId)
{
    auto task = std::make_shared<TBlockingTask>();
    BlockingTasks.push_back(task);
    Json::Value request;
    request["device_id"] = deviceId;
    TaskRunner->RunTask(request, TMakeTask(task));
    EXPECT_TRUE(task->WaitStarted(WAIT_TIMEOUT));
    return task;
}

std::string TRPCTemplatesApplyTest::CallChangingNothing(const std::function<PAsyncAnswer()>& request)
{
    auto configFileBefore = ReadConfigFile();
    auto templatesBefore = ReadDir(UserTemplatesDir);
    auto configBefore = Core->GetConfig();
    auto templateBefore = DescribeTemplate(Templates->GetTemplate(TYPE));
    std::map<std::string, size_t> closeCountsBefore;
    for (const auto& port: Ports) {
        port.second->ClearWrites();
        closeCountsBefore[port.first] = port.second->GetCloseCount();
    }

    auto error = request()->Get();

    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_EQ(templatesBefore, ReadDir(UserTemplatesDir));
    EXPECT_EQ(configBefore, Core->GetConfig());
    EXPECT_EQ(templateBefore, DescribeTemplate(Templates->GetTemplate(TYPE)));
    for (const auto& port: Ports) {
        EXPECT_TRUE(port.second->GetWrites().empty()) << port.first;
        // The devices are read through the port of the polling, it is left open for it
        EXPECT_EQ(closeCountsBefore[port.first], port.second->GetCloseCount()) << port.first;
    }
    return error;
}

void TRPCTemplatesApplyTest::ExpectRefusedWithProblems(const std::function<PAsyncAnswer()>& request,
                                                       const Json::Value& problems)
{
    auto error = CallChangingNothing(request);
    ASSERT_EQ(0u, error.rfind(APPLY_PROBLEMS_PREFIX, 0)) << error;
    EXPECT_EQ(SerializeJson(problems), SerializeJson(ParseJson(error.substr(APPLY_PROBLEMS_PREFIX.size()))));
}

void TRPCTemplatesApplyTest::ExpectAppliedWithProblems(const Json::Value& templateRoot, const Json::Value& problems)
{
    ExpectRefusedWithProblems([&]() { return Upload(templateRoot, false); }, problems);
    auto upload = Upload(templateRoot, true);
    ASSERT_EQ("", upload->Get());
    ExpectConfigInResponse(upload);
}

void TRPCTemplatesApplyTest::ExpectConfigInResponse(const PAsyncAnswer& answer)
{
    EXPECT_EQ(SerializeJson(ReadConfigJson()), SerializeJson(answer->GetResult()["config"]));
}

//! The polling is started without the template of the type the configuration uses
class TRPCTemplatesApplyWithoutPollingTest: public TRPCTemplatesApplyTest
{
protected:
    Json::Value InitialConfig() override
    {
        return MakeConfig({MakePort(PORT1, {MakeDevice(USER_TYPE, 1, "user-device")})});
    }
};

//! The polling is not started because of the device of the other type without a template
class TRPCTemplatesApplyBrokenOtherDeviceTest: public TRPCTemplatesApplyTest
{
protected:
    Json::Value InitialConfig() override
    {
        return MakeConfig({MakePort(PORT1, {MakeStockDevice(1), MakeDevice(USER_TYPE, 2, "user-device")})});
    }
};

//! The application waits for the port tasks until the test releases them
class TRPCTemplatesApplyHeldTest: public TRPCTemplatesApplyTest
{
protected:
    TRPCTemplatesApplyHeldTest()
    {
        PortTasksWaitTimeout = WAIT_TIMEOUT;
    }
};

TEST_F(TRPCTemplatesApplyTest, UnusedTypeDoesNotRestartPolling)
{
    // The device would be in the problems if it were read
    Ports[PORT1]->SetSilentFrom(1, 0);
    auto configFileBefore = ReadConfigFile();
    auto configBefore = Core->GetConfig();

    auto upload = Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), false);
    EXPECT_EQ("", upload->Get());
    auto result = upload->GetResult();
    EXPECT_EQ(USER_TYPE, result["types"][0]["types"][0]["type"].asString());
    ExpectConfigInResponse(upload);
    EXPECT_TRUE(std::filesystem::exists(UserTemplatesDir / (USER_TYPE + ".json")));
    EXPECT_TRUE(Templates->GetTemplate(USER_TYPE)->IsUserDefined());

    auto deleteAnswer = Delete(USER_TYPE, false);
    EXPECT_EQ("", deleteAnswer->Get());
    EXPECT_EQ(0u, deleteAnswer->GetResult()["types"].size());
    ExpectConfigInResponse(deleteAnswer);
    EXPECT_FALSE(std::filesystem::exists(UserTemplatesDir / (USER_TYPE + ".json")));

    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_EQ(configBefore, Core->GetConfig());
}

TEST_F(TRPCTemplatesApplyTest, UsedTypeWithoutForceIsTemplateInUse)
{
    EXPECT_EQ("template-in-use", CallChangingNothing([&]() { return Upload(MakeNewTemplate(), false); }));
    EXPECT_TRUE(ReadDir(UserTemplatesDir).empty());
}

TEST_F(TRPCTemplatesApplyTest, ParametersAreReadByNewTemplate)
{
    Ports[PORT1]->ClearWrites();
    auto configBefore = Core->GetConfig();
    auto upload = Upload(MakeNewTemplate(), true);
    ASSERT_EQ("", upload->Get());
    auto result = upload->GetResult();
    EXPECT_EQ(TYPE, result["types"][0]["types"][0]["type"].asString());
    ExpectConfigInResponse(upload);

    // The values of the configuration are not taken, p2 of the old template is removed
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(30, device["p1"].asInt());
    EXPECT_EQ(40, device["p3"].asInt());
    EXPECT_FALSE(device.isMember("p2"));
    EXPECT_EQ(TYPE + "-1", device["id"].asString());

    EXPECT_EQ(SerializeJson(MakeNewTemplate()),
              SerializeJson(ParseJson(ReadFile((UserTemplatesDir / TEMPLATE_FILE).string()))));
    EXPECT_TRUE(Templates->GetTemplate(TYPE)->IsUserDefined());
    EXPECT_NE(configBefore, Core->GetConfig());
    ASSERT_TRUE(FindPolledDevice(TYPE, "1"));

    // The polling sets the device up with the read values by the new template
    EXPECT_TRUE(WaitForWrites({{1, 20, 30}, {1, 100, 40}}));
    auto writes = Ports[PORT1]->GetWrites();
    EXPECT_EQ(writes.end(), std::find(writes.begin(), writes.end(), TModbusSimulatorPort::TWrite{1, 20, 5}));
}

TEST_F(TRPCTemplatesApplyTest, DeletedOverrideIsReplacedByStockTemplate)
{
    ASSERT_EQ("", Upload(MakeNewTemplate(), true)->Get());
    Ports[PORT1]->SetRegister(1, 10, 9);
    Ports[PORT1]->SetRegister(1, 11, 10);

    EXPECT_EQ("template-in-use", Delete(TYPE, false)->Get());
    auto deleteAnswer = Delete(TYPE, true);
    ASSERT_EQ("", deleteAnswer->Get());
    auto result = deleteAnswer->GetResult();
    EXPECT_FALSE(result["types"][0]["types"][0].isMember("user-defined"));
    ExpectConfigInResponse(deleteAnswer);

    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(9, device["p1"].asInt());
    EXPECT_EQ(10, device["p2"].asInt());
    EXPECT_FALSE(device.isMember("p3"));
    EXPECT_TRUE(ReadDir(UserTemplatesDir).empty());
    EXPECT_FALSE(Templates->GetTemplate(TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesApplyTest, UsedTypeWithoutHw)
{
    // The device would be in the problems if it were read
    Ports[PORT1]->SetSilentFrom(1, 0);
    auto withoutHw = MakeTemplateWithoutHw(TYPE, {MakeParameter("p1", 20, 0, 100), MakeParameter("p2", 21)});
    auto configFileBefore = ReadConfigFile();

    EXPECT_EQ("template-in-use", CallChangingNothing([&]() { return Upload(withoutHw, false); }));

    auto configBefore = Core->GetConfig();
    auto upload = Upload(withoutHw, true);
    ASSERT_EQ("", upload->Get());
    ExpectConfigInResponse(upload);
    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_TRUE(Templates->GetTemplate(TYPE)->IsUserDefined());
    EXPECT_NE(configBefore, Core->GetConfig());
    EXPECT_TRUE(FindPolledDevice(TYPE, "1"));
}

TEST_F(TRPCTemplatesApplyTest, UsedTypeWithoutHwFailsCheck)
{
    // p1 = 5 of the configuration is out of the range of the new template
    auto withoutHw = MakeTemplateWithoutHw(TYPE, {MakeParameter("p1", 20, 10, 100), MakeParameter("p2", 21)});

    ExpectRefusedWithProblems([&]() { return Upload(withoutHw, false); }, MakeProblems({}, {}, {{PORT1, 1}}));

    ASSERT_EQ("", Upload(withoutHw, true)->Get());
    EXPECT_EQ(5, ReadConfigJson()["ports"][0]["devices"][0]["p1"].asInt());
    EXPECT_FALSE(Core->GetConfig());
}

TEST_F(TRPCTemplatesApplyTest, LoadConfigRejectsTypeWithoutHw)
{
    auto withoutHw = MakeTemplateWithoutHw(TYPE, {MakeParameter("p1", 20, 0, 100), MakeParameter("p2", 21)});
    ASSERT_EQ("", Upload(withoutHw, true)->Get());
    // The settings would be read otherwise
    Ports[PORT1]->SetSilentFrom(1, 0);

    Json::Value request;
    request["device_id"] = FindPolledDevice(TYPE, "1")->DeviceConfig()->Id;
    auto error = CallAsync(*Rpc, "device/LoadConfig", request)->Get();
    EXPECT_NE(std::string::npos, error.find("\"hw\"")) << error;
}

TEST_F(TRPCTemplatesApplyTest, DisabledDevice)
{
    auto disabled = MakeStockDevice(2);
    disabled["enabled"] = false;
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1), disabled})}));

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({{PORT1, 2}}, {}, {}));
    auto devices = ReadConfigJson()["ports"][0]["devices"];
    EXPECT_EQ(30, devices[0]["p1"].asInt());
    EXPECT_EQ(5, devices[1]["p1"].asInt());
    EXPECT_EQ(6, devices[1]["p2"].asInt());
    EXPECT_FALSE(devices[1]["enabled"].asBool());
    EXPECT_TRUE(FindPolledDevice(TYPE, "1"));
}

TEST_F(TRPCTemplatesApplyTest, DeviceOnDisabledPort)
{
    auto port2 = MakePort(PORT2, {MakeStockDevice(3)});
    port2["enabled"] = false;
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1)}), port2}));

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({{PORT2, 3}}, {}, {}));
    auto config = ReadConfigJson();
    EXPECT_EQ(30, config["ports"][0]["devices"][0]["p1"].asInt());
    EXPECT_EQ(5, config["ports"][1]["devices"][0]["p1"].asInt());
    EXPECT_EQ(6, config["ports"][1]["devices"][0]["p2"].asInt());
}

TEST_F(TRPCTemplatesApplyTest, PortIsNotOpened)
{
    Ports[PORT2]->SetAllowOpen(false);
    auto otherDevice = MakeStockDevice(1);
    otherDevice["id"] = "other-device";
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1)}), MakePort(PORT2, {otherDevice})}));

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({}, {{PORT2, 1}}, {}));
    auto config = ReadConfigJson();
    EXPECT_EQ(30, config["ports"][0]["devices"][0]["p1"].asInt());
    EXPECT_EQ(5, config["ports"][1]["devices"][0]["p1"].asInt());
    EXPECT_EQ(6, config["ports"][1]["devices"][0]["p2"].asInt());
}

TEST_F(TRPCTemplatesApplyTest, DeviceDoesNotRespond)
{
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1), MakeStockDevice(5)})}));

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({}, {{PORT1, 5}}, {}));
    auto devices = ReadConfigJson()["ports"][0]["devices"];
    EXPECT_EQ(30, devices[0]["p1"].asInt());
    EXPECT_EQ(5, devices[1]["p1"].asInt());
    EXPECT_EQ(6, devices[1]["p2"].asInt());
}

TEST_F(TRPCTemplatesApplyTest, DeviceStopsRespondingDuringReading)
{
    // p1 is read, p3 is not
    Ports[PORT1]->SetSilentFrom(1, 100);

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({}, {{PORT1, 1}}, {}));
    // None of the read values are taken
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(5, device["p1"].asInt());
    EXPECT_EQ(6, device["p2"].asInt());
    EXPECT_FALSE(device.isMember("p3"));
}

TEST_F(TRPCTemplatesApplyTest, DeviceModelDoesNotMatch)
{
    Ports[PORT1]->SetString(1, MODEL_ADDRESS, "OTHERMO");

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({}, {{PORT1, 1}}, {}));
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(5, device["p1"].asInt());
    EXPECT_FALSE(device.isMember("p3"));
}

TEST_F(TRPCTemplatesApplyTest, FirmwareIsOlderThanTemplateRequires)
{
    auto wbTemplate = MakeNewTemplate();
    wbTemplate["hw"][0]["fw"] = "2.0.0";
    Ports[PORT1]->SetString(1, FW_ADDRESS, "1.0.0");

    ExpectAppliedWithProblems(wbTemplate, MakeProblems({}, {{PORT1, 1}}, {}));
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(5, device["p1"].asInt());
    EXPECT_FALSE(device.isMember("p3"));
}

TEST_F(TRPCTemplatesApplyTest, ValueIsOutOfRange)
{
    Ports[PORT1]->SetRegister(1, 20, 500);

    ExpectAppliedWithProblems(MakeNewTemplate(), MakeProblems({}, {}, {{PORT1, 1}}));
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(500, device["p1"].asInt());
    EXPECT_EQ(40, device["p3"].asInt());
    EXPECT_TRUE(Templates->GetTemplate(TYPE)->IsUserDefined());
    EXPECT_FALSE(Core->GetConfig());
}

TEST_F(TRPCTemplatesApplyTest, RequiredParameterIsNotRead)
{
    // The required parameter is not supported by the device, so the device does not pass the check
    Ports[PORT1]->SetUnsupported(1, 20);
    auto parameter = MakeParameter("p1", 20);
    parameter["required"] = true;
    auto requiredTemplate = MakeTemplate(TYPE, {parameter, MakeParameter("p3", 100)});

    ExpectAppliedWithProblems(requiredTemplate, MakeProblems({}, {}, {{PORT1, 1}}));
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_FALSE(device.isMember("p1"));
    EXPECT_EQ(40, device["p3"].asInt());
    EXPECT_FALSE(Core->GetConfig());
}

TEST_F(TRPCTemplatesApplyTest, UnsupportedRegisterIsNotProblem)
{
    Ports[PORT1]->SetUnsupported(1, 100);

    EXPECT_EQ("template-in-use", Upload(MakeNewTemplate(), false)->Get());
    ASSERT_EQ("", Upload(MakeNewTemplate(), true)->Get());
    auto device = ReadConfigJson()["ports"][0]["devices"][0];
    EXPECT_EQ(30, device["p1"].asInt());
    EXPECT_FALSE(device.isMember("p3"));
}

TEST_F(TRPCTemplatesApplyTest, SeveralProblems)
{
    Ports[PORT1]->SetRegister(1, 20, 500);
    auto disabled = MakeStockDevice(2);
    disabled["enabled"] = false;
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1), disabled, MakeStockDevice(5)})}));

    ExpectRefusedWithProblems([&]() { return Upload(MakeNewTemplate(), false); },
                              MakeProblems({{PORT1, 2}}, {{PORT1, 5}}, {{PORT1, 1}}));
}

TEST_F(TRPCTemplatesApplyTest, TcpPortInProblems)
{
    Json::Value tcpPort;
    tcpPort["port_type"] = "tcp";
    tcpPort["address"] = "192.168.1.10";
    tcpPort["port"] = 2000;
    tcpPort["enabled"] = false;
    tcpPort["devices"].append(MakeStockDevice(4));
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1)}), tcpPort}));

    ExpectRefusedWithProblems([&]() { return Upload(MakeNewTemplate(), false); },
                              MakeProblems({{"192.168.1.10:2000", 4}}, {}, {}));
}

TEST_F(TRPCTemplatesApplyTest, DeletedSoleTemplate)
{
    Ports[PORT1]->AddDevice(2);
    Ports[PORT1]->SetString(2, MODEL_ADDRESS, MODEL);
    ASSERT_EQ("", Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), false)->Get());
    auto userDevice = MakeDevice(USER_TYPE, 2, "user-device");
    userDevice["p1"] = 3;
    SaveAndWaitSetup(MakeConfig({MakePort(PORT1, {MakeStockDevice(1), userDevice})}),
                     {{1, 10, 5}, {1, 11, 6}, {2, 20, 3}});
    // The device would be in the problems if it were read
    Ports[PORT1]->SetSilentFrom(2, 0);
    auto userTemplateBefore = DescribeTemplate(Templates->GetTemplate(USER_TYPE));

    EXPECT_EQ("template-in-use", CallChangingNothing([&]() { return Delete(USER_TYPE, false); }));
    EXPECT_EQ(userTemplateBefore, DescribeTemplate(Templates->GetTemplate(USER_TYPE)));

    auto configFileBefore = ReadConfigFile();
    auto deleteAnswer = Delete(USER_TYPE, true);
    ASSERT_EQ("", deleteAnswer->Get());
    EXPECT_EQ(0u, deleteAnswer->GetResult()["types"].size());
    ExpectConfigInResponse(deleteAnswer);

    // The configuration is kept as is, nothing is polled
    EXPECT_FALSE(std::filesystem::exists(UserTemplatesDir / (USER_TYPE + ".json")));
    EXPECT_THROW(Templates->GetTemplate(USER_TYPE), std::out_of_range);
    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_FALSE(Core->GetConfig());
    EXPECT_FALSE(Core->IsBusy());

    Json::Value empty(Json::objectValue);
    EXPECT_NO_THROW(Rpc->Call("config/Load", empty));

    // The fixed configuration starts the polling
    EXPECT_EQ("", Save(InitialConfig()));
    EXPECT_TRUE(FindPolledDevice(TYPE, "1"));
}

TEST_F(TRPCTemplatesApplyTest, PortBusy)
{
    auto blocking = RunBlockingTask(FindPolledDevice(TYPE, "1")->DeviceConfig()->Id);

    EXPECT_EQ("port-busy", CallChangingNothing([&]() { return Upload(MakeNewTemplate(), true); }));

    EXPECT_TRUE(ReadDir(UserTemplatesDir).empty());
    EXPECT_FALSE(Templates->GetTemplate(TYPE)->IsUserDefined());
    blocking->Release();
}

TEST_F(TRPCTemplatesApplyTest, TemplateWriteFailure)
{
    // A directory in place of the temporary file can not be opened for writing
    std::filesystem::create_directories(UserTemplatesDir / (TEMPLATE_FILE + ".tmp/dir"));

    EXPECT_EQ("write-failed", CallChangingNothing([&]() { return Upload(MakeNewTemplate(), true); }));

    EXPECT_FALSE(Templates->GetTemplate(TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesApplyTest, ConfigWriteFailure)
{
    // Neither the upload nor the deletion changes anything
    ASSERT_EQ("", Upload(MakeNewTemplate(), true)->Get());
    // The read values differ from the configuration, so it is written
    ASSERT_TRUE(WaitForWrites({{1, 20, 30}, {1, 100, 40}}));
    Ports[PORT1]->SetRegister(1, 20, 31);
    auto updatedTemplate = MakeNewTemplate();
    updatedTemplate["title"] = "Updated";
    std::filesystem::create_directories(ConfigPath + ".tmp/dir");
    auto configFileBefore = ReadConfigFile();
    auto templatesBefore = ReadDir(UserTemplatesDir);
    auto configBefore = Core->GetConfig();
    auto templateBefore = DescribeTemplate(Templates->GetTemplate(TYPE));

    EXPECT_EQ("write-failed", Upload(updatedTemplate, true)->Get());
    EXPECT_EQ("write-failed", Delete(TYPE, true)->Get());

    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_EQ(templatesBefore, ReadDir(UserTemplatesDir));
    EXPECT_EQ(configBefore, Core->GetConfig());
    EXPECT_EQ(templateBefore, DescribeTemplate(Templates->GetTemplate(TYPE)));

    // The polling goes on
    auto blocking = RunBlockingTask(FindPolledDevice(TYPE, "1")->DeviceConfig()->Id);
    blocking->Release();
}

TEST_F(TRPCTemplatesApplyTest, UnusedTypeTemplateWriteFailure)
{
    std::filesystem::create_directories(UserTemplatesDir / (USER_TYPE + ".json.tmp/dir"));
    auto configBefore = Core->GetConfig();

    EXPECT_EQ("write-failed", Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), false)->Get());

    EXPECT_THROW(Templates->GetTemplate(USER_TYPE), std::out_of_range);
    EXPECT_FALSE(std::filesystem::exists(UserTemplatesDir / (USER_TYPE + ".json")));
    EXPECT_EQ(configBefore, Core->GetConfig());
}

TEST_F(TRPCTemplatesApplyTest, UsedTypeIsUploadedUnderOtherName)
{
    ASSERT_EQ("", Upload(MakeNewTemplate(), true)->Get());
    // The read values differ from the configuration, so it is written
    ASSERT_TRUE(WaitForWrites({{1, 20, 30}, {1, 100, 40}}));
    Ports[PORT1]->SetRegister(1, 20, 31);
    auto updatedTemplate = MakeNewTemplate();
    updatedTemplate["title"] = "Updated";

    std::filesystem::create_directories(ConfigPath + ".tmp/dir");
    EXPECT_EQ("write-failed", Upload(updatedTemplate, true, "renamed.json")->Get());
    auto files = ReadDir(UserTemplatesDir);
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ(TEMPLATE_FILE, files.begin()->first);
    EXPECT_EQ(TYPE, Templates->GetTemplate(TYPE)->GetTitle());

    std::filesystem::remove_all(ConfigPath + ".tmp");
    ASSERT_EQ("", Upload(updatedTemplate, true, "renamed.json")->Get());
    files = ReadDir(UserTemplatesDir);
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ("renamed.json", files.begin()->first);
    EXPECT_EQ("Updated", Templates->GetTemplate(TYPE)->GetTitle());
    EXPECT_EQ((UserTemplatesDir / "renamed.json").string(), Templates->GetTemplate(TYPE)->GetFilePath());
}

TEST_F(TRPCTemplatesApplyTest, LoadConfigDoesNotReturnValuesReadBefore)
{
    Json::Value request;
    request["device_id"] = FindPolledDevice(TYPE, "1")->DeviceConfig()->Id;
    auto before = CallAsync(*Rpc, "device/LoadConfig", request);
    ASSERT_EQ("", before->Get());
    EXPECT_EQ(5, before->GetResult()["parameters"]["p1"].asInt());

    ASSERT_EQ("", Upload(MakeNewTemplate(), true)->Get());

    request["device_id"] = FindPolledDevice(TYPE, "1")->DeviceConfig()->Id;
    auto after = CallAsync(*Rpc, "device/LoadConfig", request);
    ASSERT_EQ("", after->Get());
    auto parameters = after->GetResult()["parameters"];
    EXPECT_EQ(30, parameters["p1"].asInt());
    EXPECT_EQ(40, parameters["p3"].asInt());
    EXPECT_FALSE(parameters.isMember("p2"));
}

TEST_F(TRPCTemplatesApplyHeldTest, RequestsAreRefusedDuringApplication)
{
    auto deviceId = FindPolledDevice(TYPE, "1")->DeviceConfig()->Id;
    auto blocking = RunBlockingTask(deviceId);
    auto upload = Upload(MakeNewTemplate(), true);
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (!Core->IsBusy()) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(10ms);
    }

    Json::Value deviceRequest;
    deviceRequest["device_id"] = deviceId;
    auto loadConfig = CallAsync(*Rpc, "device/LoadConfig", deviceRequest);
    ASSERT_TRUE(loadConfig->IsAnswered());
    EXPECT_EQ(CONFIG_BUSY_ERROR, loadConfig->Get());
    Json::Value empty(Json::objectValue);
    EXPECT_THROW(Rpc->Call("config/Load", empty), std::exception);

    // The other configuration changes wait for the application
    Json::Value saveRequest;
    saveRequest["config"] = InitialConfig();
    auto save = CallAsync(*Rpc, "config/Save", saveRequest);
    auto otherUpload = Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), false);

    EXPECT_FALSE(upload->IsAnswered());
    EXPECT_FALSE(save->IsAnswered());
    EXPECT_FALSE(otherUpload->IsAnswered());
    blocking->Release();
    EXPECT_EQ("", upload->Get());
    EXPECT_EQ("", save->Get());
    EXPECT_EQ("", otherUpload->Get());
    EXPECT_NO_THROW(Rpc->Call("config/Load", empty));
}

TEST_F(TRPCTemplatesApplyHeldTest, ApplicationWaitsForSave)
{
    auto blocking = RunBlockingTask(FindPolledDevice(TYPE, "1")->DeviceConfig()->Id);
    Json::Value saveRequest;
    saveRequest["config"] = InitialConfig();
    auto save = CallAsync(*Rpc, "config/Save", saveRequest);
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (!Core->IsBusy()) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline);
        std::this_thread::sleep_for(10ms);
    }

    auto upload = Upload(MakeNewTemplate(), true);
    EXPECT_FALSE(upload->IsAnswered());

    blocking->Release();
    EXPECT_EQ("", save->Get());
    EXPECT_EQ("", upload->Get());
    EXPECT_EQ(1u, ReadDir(UserTemplatesDir).size());
}

TEST_F(TRPCTemplatesApplyWithoutPollingTest, UploadStartsPolling)
{
    EXPECT_FALSE(Core->GetConfig());

    auto upload = Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), true);
    ASSERT_EQ("", upload->Get());
    ExpectConfigInResponse(upload);

    EXPECT_EQ(30, ReadConfigJson()["ports"][0]["devices"][0]["p1"].asInt());
    EXPECT_TRUE(FindPolledDevice(USER_TYPE, "1"));
}

TEST_F(TRPCTemplatesApplyWithoutPollingTest, ReadingTakesPortSettingsOfConfig)
{
    // The executor of the port is made by an RPC request with other settings
    Json::Value request;
    request["path"] = PORT1;
    request["baud_rate"] = 1200;
    request["parity"] = "N";
    request["data_bits"] = 8;
    request["stop_bits"] = 1;
    auto task = std::make_shared<TBlockingTask>();
    TaskRunner->RunTask(request, TMakeTask(task));
    ASSERT_TRUE(task->WaitStarted(WAIT_TIMEOUT));
    task->Release();

    ASSERT_EQ("", Upload(MakeTemplate(USER_TYPE, {MakeParameter("p1", 20)}), true)->Get());
    EXPECT_EQ(30, ReadConfigJson()["ports"][0]["devices"][0]["p1"].asInt());
    EXPECT_EQ(std::vector<int>{TSerialPortConnectionSettings().BaudRate}, Ports[PORT1]->GetAppliedBaudRates());
}

TEST_F(TRPCTemplatesApplyBrokenOtherDeviceTest, OtherDeviceErrorIsNotProblem)
{
    EXPECT_FALSE(Core->GetConfig());
    auto configFileBefore = ReadConfigFile();

    EXPECT_EQ("template-in-use", Upload(MakeNewTemplate(), false)->Get());
    EXPECT_EQ(configFileBefore, ReadConfigFile());
    EXPECT_TRUE(ReadDir(UserTemplatesDir).empty());

    auto upload = Upload(MakeNewTemplate(), true);
    ASSERT_EQ("", upload->Get());
    ExpectConfigInResponse(upload);
    auto devices = ReadConfigJson()["ports"][0]["devices"];
    EXPECT_EQ(30, devices[0]["p1"].asInt());
    EXPECT_EQ(40, devices[0]["p3"].asInt());
    EXPECT_EQ(USER_TYPE, devices[1]["device_type"].asString());
    EXPECT_TRUE(Templates->GetTemplate(TYPE)->IsUserDefined());
    EXPECT_FALSE(Core->GetConfig());

    // The fixed configuration starts the polling
    auto config = ReadConfigJson();
    config["ports"][0]["devices"].resize(1);
    EXPECT_EQ("", Save(config));
    EXPECT_TRUE(FindPolledDevice(TYPE, "1"));
}
