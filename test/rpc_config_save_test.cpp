#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <set>
#include <thread>

#include <wblib/driver.h>
#include <wblib/driver_args.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

#include "file_utils.h"
#include "rpc/rpc_config_handler.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_fw_update_handler.h"
#include "rpc/rpc_helpers.h"
#include "rpc/rpc_port_handler.h"
#include "rpc/rpc_templates_handler.h"
#include "serial_config.h"
#include "test_utils.h"

using namespace std::chrono_literals;
using WBMQTT::Testing::TLoggedFixture;

namespace
{
    //! config/Save waits for the accepted port requests this long, unless a test sets its own timeout
    const auto PORT_TASKS_WAIT_TIMEOUT = 300ms;

    //! The tests run under valgrind, the waits for other threads are generous
    const auto WAIT_TIMEOUT = 30s;

    const std::string PORT1 = "/dev/ttyConfigSaveTest1";
    const std::string PORT2 = "/dev/ttyConfigSaveTest2";
    const std::string PORT3 = "/dev/ttyConfigSaveTest3";
    const std::string DEVICE1 = "config-save-test-device1";
    const std::string DEVICE2 = "config-save-test-device2";
    const std::string DEVICE3 = "config-save-test-device3";
    const std::string CHANNEL = "Register";

    //! Keeps the methods as the handlers register them, the test calls them the way the RPC server does
    class TRPCMethods: public WBMQTT::TMqttRpcServer
    {
    public:
        void RegisterMethod(const std::string& service, const std::string& method, TMethodHandler handler) override
        {
            Methods[service + "/" + method] = handler;
        }

        void RegisterAsyncMethod(const std::string& service,
                                 const std::string& method,
                                 TAsyncMethodHandler handler) override
        {
            AsyncMethods[service + "/" + method] = handler;
        }

        void Start() override
        {}

        void Stop() override
        {}

        Json::Value Call(const std::string& method, const Json::Value& params)
        {
            return Methods.at(method)(params);
        }

        void CallAsync(const std::string& method,
                       const Json::Value& params,
                       TResultCallback onResult,
                       TErrorCallback onError)
        {
            AsyncMethods.at(method)(params, onResult, onError);
        }

    private:
        std::map<std::string, TMethodHandler> Methods;
        std::map<std::string, TAsyncMethodHandler> AsyncMethods;
    };

    class TNoNetworkHttpClient: public IHttpClient
    {
    public:
        std::string GetText(const std::string& url) override
        {
            throw std::runtime_error("No network in tests: " + url);
        }

        std::vector<uint8_t> GetBinary(const std::string& url) override
        {
            throw std::runtime_error("No network in tests: " + url);
        }
    };

    //! Passes everything to the driver and counts the handlers of the control values written over MQTT
    class TCountingDriver: public WBMQTT::TDeviceDriver
    {
    public:
        TCountingDriver(WBMQTT::PDeviceDriver driver): Driver(driver)
        {}

        const std::string& GetId() const override
        {
            return Driver->GetId();
        }

        void SetFilter(const WBMQTT::PDeviceFilter& filter) override
        {
            Driver->SetFilter(filter);
        }

        void StartLoop() override
        {
            Driver->StartLoop();
        }

        void StopLoop() override
        {
            Driver->StopLoop();
        }

        WBMQTT::PDriverTx BeginTx() override
        {
            return Driver->BeginTx();
        }

        void Access(const WBMQTT::TThunkFunction& thunk) override
        {
            Driver->Access(thunk);
        }

        WBMQTT::TFuture<void> AccessAsync(const WBMQTT::TThunkFunction& thunk) override
        {
            return Driver->AccessAsync(thunk);
        }

        void OnRetainReady(const WBMQTT::TThunkFunction& thunk) override
        {
            Driver->OnRetainReady(thunk);
        }

        void WaitForReady() override
        {
            Driver->WaitForReady();
        }

        WBMQTT::PDriverEventHandlerHandle OnDriverEvent(WBMQTT::EDriverEventType type,
                                                        const WBMQTT::TDriverEventHandler& handler) override
        {
            auto handle = Driver->OnDriverEvent(type, handler);
            if (type == WBMQTT::TControlOnValueEvent::StaticGetType()) {
                std::lock_guard<std::mutex> lock(Mutex);
                ControlOnValueHandlers.insert(handle);
            }
            return handle;
        }

        void RemoveEventHandler(const WBMQTT::PDriverEventHandlerHandle& handle) override
        {
            {
                std::lock_guard<std::mutex> lock(Mutex);
                ControlOnValueHandlers.erase(handle);
            }
            Driver->RemoveEventHandler(handle);
        }

        void Close() override
        {
            Driver->Close();
        }

        WBMQTT::TLocalDeviceFactory SetLocalDeviceFactory(WBMQTT::TLocalDeviceFactory factory) override
        {
            return Driver->SetLocalDeviceFactory(factory);
        }

        WBMQTT::TExternalDeviceFactory SetExternalDeviceFactory(WBMQTT::TExternalDeviceFactory factory) override
        {
            return Driver->SetExternalDeviceFactory(factory);
        }

        WBMQTT::TControlFactory SetControlFactory(WBMQTT::TControlFactory factory) override
        {
            return Driver->SetControlFactory(factory);
        }

        WBMQTT::PDriverTx CreateUnsafeTx() override
        {
            return Driver->CreateUnsafeTx();
        }

        bool NeedToReownUnknownDevices() const override
        {
            return Driver->NeedToReownUnknownDevices();
        }

        size_t GetControlOnValueHandlersCount()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            return ControlOnValueHandlers.size();
        }

    private:
        WBMQTT::PDeviceDriver Driver;
        std::mutex Mutex;
        std::set<WBMQTT::PDriverEventHandlerHandle> ControlOnValueHandlers;
    };

    //! Occupies the port until Release, as a long exchange with a device does
    class TBlockingTask: public ISerialClientTask
    {
    public:
        ISerialClientTask::TRunResult Run(PFeaturePort port,
                                          TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                          const std::list<PSerialDevice>& polledDevices) override
        {
            Started.set_value();
            ReleaseFuture.wait();
            return ISerialClientTask::TRunResult::OK;
        }

        bool WaitStarted(std::chrono::milliseconds timeout)
        {
            return StartedFuture.wait_for(timeout) == std::future_status::ready;
        }

        void Release()
        {
            if (!Released.exchange(true)) {
                ReleasePromise.set_value();
            }
        }

    private:
        std::promise<void> Started;
        std::future<void> StartedFuture = Started.get_future();
        std::promise<void> ReleasePromise;
        std::shared_future<void> ReleaseFuture = ReleasePromise.get_future().share();
        std::atomic<bool> Released{false};
    };

    //! Records the port and the polled devices it is run with, a cancelled task does nothing
    class TRecordingTask: public ISerialClientTask
    {
    public:
        ISerialClientTask::TRunResult Run(PFeaturePort port,
                                          TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                          const std::list<PSerialDevice>& polledDevices) override
        {
            if (IsCancelled()) {
                Cancelled.set_value();
                return ISerialClientTask::TRunResult::OK;
            }
            {
                std::lock_guard<std::mutex> lock(Mutex);
                Port = port;
                PolledDevices = polledDevices;
            }
            if (++Runs == 1) {
                Done.set_value();
            }
            return ISerialClientTask::TRunResult::OK;
        }

        bool WaitCancelled(std::chrono::milliseconds timeout)
        {
            return CancelledFuture.wait_for(timeout) == std::future_status::ready;
        }

        int GetRuns() const
        {
            return Runs;
        }

        bool WaitDone(std::chrono::milliseconds timeout)
        {
            return DoneFuture.wait_for(timeout) == std::future_status::ready;
        }

        std::string GetPortPath()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            return Port ? Port->GetDescription(false) : std::string();
        }

        std::list<PSerialDevice> GetPolledDevices()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            return PolledDevices;
        }

    private:
        std::mutex Mutex;
        PFeaturePort Port;
        std::list<PSerialDevice> PolledDevices;
        std::atomic<int> Runs{0};
        std::promise<void> Done;
        std::future<void> DoneFuture = Done.get_future();
        std::promise<void> Cancelled;
        std::future<void> CancelledFuture = Cancelled.get_future();
    };

    //! The answer of an asynchronous RPC method, an empty error on success
    class TAsyncAnswer
    {
    public:
        void Set(const std::string& error)
        {
            if (!Answered.exchange(true)) {
                Error.set_value(error);
            }
        }

        bool IsAnswered() const
        {
            return Answered;
        }

        bool Wait(std::chrono::milliseconds timeout) const
        {
            return ErrorFuture.wait_for(timeout) == std::future_status::ready;
        }

        //! Waits for the answer, "no answer" if there is none in WAIT_TIMEOUT
        std::string Get() const
        {
            return Wait(WAIT_TIMEOUT) ? ErrorFuture.get() : std::string("no answer");
        }

    private:
        std::promise<std::string> Error;
        std::shared_future<std::string> ErrorFuture = Error.get_future().share();
        std::atomic<bool> Answered{false};
    };

    typedef std::shared_ptr<TAsyncAnswer> PAsyncAnswer;

    Json::Value MakeDevice(const std::string& id, int slaveId)
    {
        Json::Value device;
        device["name"] = id;
        device["id"] = id;
        device["slave_id"] = std::to_string(slaveId);
        Json::Value channel;
        channel["name"] = CHANNEL;
        channel["reg_type"] = "holding";
        channel["address"] = 0;
        device["channels"].append(channel);
        return device;
    }

    Json::Value MakePort(const std::string& path, const std::vector<std::string>& deviceIds)
    {
        Json::Value port;
        port["path"] = path;
        port["devices"] = Json::Value(Json::arrayValue);
        int slaveId = 1;
        for (const auto& id: deviceIds) {
            port["devices"].append(MakeDevice(id, slaveId++));
        }
        return port;
    }

    Json::Value MakeConfig(const std::vector<Json::Value>& ports, bool debug = false)
    {
        Json::Value config;
        config["ports"] = Json::Value(Json::arrayValue);
        for (const auto& port: ports) {
            config["ports"].append(port);
        }
        if (debug) {
            config["debug"] = true;
        }
        return config;
    }

    std::string ReadFile(const std::string& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    std::set<std::string> ListDir(const std::filesystem::path& dir)
    {
        std::set<std::string> res;
        for (const auto& entry: std::filesystem::directory_iterator(dir)) {
            res.insert(entry.path().filename().string());
        }
        return res;
    }

    Json::Value MakeSerialPortRequest(const std::string& path)
    {
        Json::Value request;
        request["path"] = path;
        request["baud_rate"] = 9600;
        request["parity"] = "N";
        request["data_bits"] = 8;
        request["stop_bits"] = 2;
        return request;
    }

    Json::Value MakeFwUpdateRequest()
    {
        Json::Value request;
        request["slave_id"] = 1;
        request["port"]["path"] = PORT1;
        return request;
    }

    //! The registers of a duplicated device are refused by the polling
    void BreakHandlerConfig(PHandlerConfig handlerConfig)
    {
        auto& devices = handlerConfig->PortConfigs.at(0)->Devices;
        devices.push_back(devices.at(0));
    }

    //! Passes the loaded configuration to the polling, the broken one can not be polled
    class TBreakingConfigLoader: public TConfigLoader
    {
    public:
        using TConfigLoader::TConfigLoader;

        PHandlerConfig Load(const Json::Value& config) override
        {
            auto handlerConfig = TConfigLoader::Load(config);
            if (BreakLoadedConfig) {
                BreakHandlerConfig(handlerConfig);
            }
            return handlerConfig;
        }

        std::atomic<bool> BreakLoadedConfig{false};
    };

    //! Gives the task made beforehand and keeps the parameters cache it is made with
    class TMakeTask
    {
    public:
        TMakeTask(PSerialClientTask task): Task(task)
        {}

        PSerialClientTask operator()(PSerialDevice, PDeviceParametersCache cache)
        {
            Cache = cache;
            return Task;
        }

        PDeviceParametersCache GetCache() const
        {
            return Cache;
        }

    private:
        PSerialClientTask Task;
        PDeviceParametersCache Cache;
    };
}

/**
 * The service as main() assembles it: the polling with the real port classes on paths which do not exist,
 * so nothing is exchanged and the port requests are the tasks of the test, and all the RPC handlers.
 * The MQTT log is not compared with a file, the tests assert the behaviour themselves
 */
class TRPCConfigSaveTest: public TLoggedFixture
{
protected:
    void SetUp() override;
    void TearDown() override;

    //! The configuration the polling starts with, null starts the service without a valid configuration
    virtual Json::Value InitialConfig();

    PHandlerConfig LoadConfigJson(const Json::Value& config);
    std::string ReadConfigFile();

    PAsyncAnswer CallAsync(const std::string& method, const Json::Value& params);

    //! error.data of a refused method, an empty string if the method returned
    std::string CallError(const std::string& method, const Json::Value& params = Json::Value(Json::objectValue));

    PAsyncAnswer StartSave(const Json::Value& config);

    //! error.data of a refused config/Save, an empty string on success
    std::string Save(const Json::Value& config);

    //! Waits until config/Save stops accepting port requests
    void WaitGateClosed();

    //! Waits until config/Save cancels the task in the queue of the port, so it has stopped the port clients
    void WaitCancelRequested(PSerialClientTask task);

    std::shared_ptr<TBlockingTask> RunBlockingTask(const Json::Value& request);
    std::shared_ptr<TBlockingTask> RunBlockingTask(const std::string& deviceId);
    //! Occupies the polled port with a task
    std::shared_ptr<TBlockingTask> OccupyPort(const std::string& path);
    std::shared_ptr<TRecordingTask> SubmitTask(const std::string& deviceId);
    std::shared_ptr<TRecordingTask> SubmitTask(const TPortSettings& portSettings);
    //! The cache the port requests are made with
    PDeviceParametersCache GetParametersCache();
    PSerialDevice FindPolledDevice(const std::string& deviceId);
    bool HasMqttDevice(const std::string& deviceId);

    std::filesystem::path TestDir;
    std::string ConfigPath;
    bool DebugWasEnabled = false;

    std::chrono::milliseconds PortTasksWaitTimeout = PORT_TASKS_WAIT_TIMEOUT;
    bool DebugFromCommandLine = false;
    //! The initial configuration passes the checks, but can not be polled
    bool BreakInitialConfig = false;

    TSerialDeviceFactory DeviceFactory;
    Json::Value CommonDeviceSchema;
    Json::Value PortsSchema;
    Json::Value GroupTranslations;
    std::unique_ptr<TProtocolConfedSchemasMap> ProtocolSchemas;
    PTemplateMap Templates;
    std::unique_ptr<TDevicesConfedSchemasMap> ConfedSchemas;

    WBMQTT::Testing::PFakeMqttBroker MqttBroker;
    WBMQTT::Testing::PFakeMqttClient MqttClient;
    WBMQTT::PDeviceDriver Driver;
    std::shared_ptr<TCountingDriver> CountingDriver;

    std::unique_ptr<TBreakingConfigLoader> ConfigLoader;
    std::unique_ptr<TSerialDriverCore> Core;
    std::unique_ptr<TSerialClientTaskRunner> TaskRunner;
    std::shared_ptr<TRPCMethods> Rpc;
    std::unique_ptr<TRPCConfigHandler> ConfigHandler;
    std::unique_ptr<TRPCTemplatesHandler> TemplatesHandler;
    std::unique_ptr<TRPCDeviceHandler> DeviceHandler;
    std::unique_ptr<TRPCPortHandler> PortHandler;
    std::unique_ptr<TRPCFwUpdateHandler> FwUpdateHandler;

    std::vector<std::shared_ptr<TBlockingTask>> BlockingTasks;
};

void TRPCConfigSaveTest::SetUp()
{
    TLoggedFixture::SetUp();
    TestDir = std::filesystem::temp_directory_path() / "wb-mqtt-serial-config-save-test" /
              ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(TestDir);
    std::filesystem::create_directories(TestDir / "templates");
    ConfigPath = (TestDir / "wb-mqtt-serial.conf").string();
    DebugWasEnabled = ::Debug.IsEnabled();
    if (DebugFromCommandLine) {
        ::Debug.SetEnabled(true);
    }

    RegisterProtocols(DeviceFactory);
    CommonDeviceSchema = GetCommonDeviceSchema();
    PortsSchema = WBMQTT::JSON::Parse(GetDataFilePath("../wb-mqtt-serial-ports.schema.json"));
    GroupTranslations = WBMQTT::JSON::Parse(GetDataFilePath("../groups.json"));
    ProtocolSchemas = std::make_unique<TProtocolConfedSchemasMap>(GetDataFilePath("../protocols"), CommonDeviceSchema);
    Templates = std::make_shared<TTemplateMap>(GetTemplatesSchema(), (TestDir / "templates").string());
    Templates->AddTemplatesDir(GetDataFilePath("device-templates"));
    ConfedSchemas = std::make_unique<TDevicesConfedSchemasMap>(*Templates, DeviceFactory, CommonDeviceSchema);

    MqttBroker = WBMQTT::Testing::NewFakeMqttBroker(*this);
    MqttClient = MqttBroker->MakeClient("config-save-test");
    Driver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
                                   .SetId("config-save-test")
                                   .SetBackend(WBMQTT::NewDriverBackend(MqttClient))
                                   .SetIsTesting(true)
                                   .SetReownUnknownDevices(true)
                                   .SetUseStorage(true)
                                   .SetStoragePath((TestDir / "libwbmqtt.db").string()));
    Driver->StartLoop();
    CountingDriver = std::make_shared<TCountingDriver>(Driver);

    auto initialConfig = InitialConfig();
    PHandlerConfig handlerConfig;
    if (initialConfig.isNull()) {
        WriteToFile(ConfigPath, "{ broken");
    } else {
        WriteToFile(ConfigPath, SerializeJson(initialConfig));
        handlerConfig = LoadConfigJson(initialConfig);
        if (BreakInitialConfig) {
            BreakHandlerConfig(handlerConfig);
        }
    }
    ConfigLoader = std::make_unique<TBreakingConfigLoader>(ConfigPath,
                                                           DeviceFactory,
                                                           CommonDeviceSchema,
                                                           *Templates,
                                                           PortsSchema,
                                                           *ProtocolSchemas);
    Core = std::make_unique<TSerialDriverCore>(CountingDriver,
                                               handlerConfig,
                                               *ConfigLoader,
                                               DebugFromCommandLine,
                                               PortTasksWaitTimeout);
    TaskRunner = std::make_unique<TSerialClientTaskRunner>(*Core);

    Rpc = std::make_shared<TRPCMethods>();
    ConfigHandler = std::make_unique<TRPCConfigHandler>(ConfigPath,
                                                        PortsSchema,
                                                        Templates,
                                                        *ConfedSchemas,
                                                        *ProtocolSchemas,
                                                        GroupTranslations,
                                                        *Core,
                                                        Rpc);
    TemplatesHandler = std::make_unique<TRPCTemplatesHandler>(
        (TestDir / "templates").string(),
        ConfigPath,
        Templates,
        *ConfedSchemas,
        GroupTranslations,
        GetDataFilePath("../wb-mqtt-serial-rpc-templates-upload-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-templates-delete-request.schema.json"),
        *Core);
    RegisterTemplatesRpcHandlers(*TemplatesHandler, Rpc);
    DeviceHandler = std::make_unique<TRPCDeviceHandler>(
        ConfigPath,
        GetDataFilePath("../wb-mqtt-serial-rpc-device-load-config-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-load-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-set-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-probe-request.schema.json"),
        GetDataFilePath("../wb-mqtt-serial-rpc-device-set-poll-request.schema.json"),
        DeviceFactory,
        Templates,
        *TaskRunner,
        Rpc);
    PortHandler =
        std::make_unique<TRPCPortHandler>(GetDataFilePath("../wb-mqtt-serial-rpc-port-load-request.schema.json"),
                                          GetDataFilePath("../wb-mqtt-serial-rpc-port-setup-request.schema.json"),
                                          GetDataFilePath("../wb-mqtt-serial-rpc-port-scan-request.schema.json"),
                                          *Core,
                                          *TaskRunner,
                                          Rpc);
    FwUpdateHandler =
        std::make_unique<TRPCFwUpdateHandler>(*TaskRunner, Rpc, MqttClient, std::make_shared<TNoNetworkHttpClient>());

    Core->Start();
}

void TRPCConfigSaveTest::TearDown()
{
    for (const auto& task: BlockingTasks) {
        task->Release();
    }
    ConfigHandler.reset();
    Core->Stop();
    FwUpdateHandler.reset();
    PortHandler.reset();
    DeviceHandler.reset();
    TemplatesHandler.reset();
    TaskRunner.reset();
    Core.reset();
    ConfigLoader.reset();
    Driver->StopLoop();
    ::Debug.SetEnabled(DebugWasEnabled);
    std::filesystem::remove_all(TestDir);
}

Json::Value TRPCConfigSaveTest::InitialConfig()
{
    return MakeConfig({MakePort(PORT1, {DEVICE1})});
}

PHandlerConfig TRPCConfigSaveTest::LoadConfigJson(const Json::Value& config)
{
    return TConfigLoader(ConfigPath, DeviceFactory, CommonDeviceSchema, *Templates, PortsSchema, *ProtocolSchemas)
        .Load(config);
}

std::string TRPCConfigSaveTest::ReadConfigFile()
{
    return ReadFile(ConfigPath);
}

PAsyncAnswer TRPCConfigSaveTest::CallAsync(const std::string& method, const Json::Value& params)
{
    auto answer = std::make_shared<TAsyncAnswer>();
    Rpc->CallAsync(
        method,
        params,
        [answer](const Json::Value&) { answer->Set(std::string()); },
        [answer](WBMQTT::TMqttRpcErrorCode, const std::string& error) { answer->Set(error); });
    return answer;
}

std::string TRPCConfigSaveTest::CallError(const std::string& method, const Json::Value& params)
{
    try {
        Rpc->Call(method, params);
    } catch (const std::exception& e) {
        return e.what();
    }
    return std::string();
}

PAsyncAnswer TRPCConfigSaveTest::StartSave(const Json::Value& config)
{
    Json::Value request;
    request["config"] = config;
    return CallAsync("config/Save", request);
}

std::string TRPCConfigSaveTest::Save(const Json::Value& config)
{
    return StartSave(config)->Get();
}

void TRPCConfigSaveTest::WaitGateClosed()
{
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (true) {
        if (Core->IsBusy()) {
            return;
        }
        ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "config/Save does not refuse port requests";
        std::this_thread::sleep_for(10ms);
    }
}

void TRPCConfigSaveTest::WaitCancelRequested(PSerialClientTask task)
{
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (!task->IsCancelled()) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline) << "config/Save does not cancel the queued task";
        std::this_thread::sleep_for(10ms);
    }
}

std::shared_ptr<TBlockingTask> TRPCConfigSaveTest::RunBlockingTask(const Json::Value& request)
{
    auto task = std::make_shared<TBlockingTask>();
    BlockingTasks.push_back(task);
    TaskRunner->RunTask(request, TMakeTask(task));
    EXPECT_TRUE(task->WaitStarted(WAIT_TIMEOUT));
    return task;
}

std::shared_ptr<TBlockingTask> TRPCConfigSaveTest::RunBlockingTask(const std::string& deviceId)
{
    Json::Value request;
    request["device_id"] = deviceId;
    return RunBlockingTask(request);
}

std::shared_ptr<TBlockingTask> TRPCConfigSaveTest::OccupyPort(const std::string& path)
{
    return RunBlockingTask(MakeSerialPortRequest(path));
}

std::shared_ptr<TRecordingTask> TRPCConfigSaveTest::SubmitTask(const std::string& deviceId)
{
    auto task = std::make_shared<TRecordingTask>();
    Json::Value request;
    request["device_id"] = deviceId;
    TaskRunner->RunTask(request, TMakeTask(task));
    return task;
}

std::shared_ptr<TRecordingTask> TRPCConfigSaveTest::SubmitTask(const TPortSettings& portSettings)
{
    auto task = std::make_shared<TRecordingTask>();
    TaskRunner->RunTask(portSettings, task);
    return task;
}

PDeviceParametersCache TRPCConfigSaveTest::GetParametersCache()
{
    auto task = std::make_shared<TRecordingTask>();
    TMakeTask makeTask(task);
    TaskRunner->RunTask(MakeSerialPortRequest(PORT1), std::ref(makeTask));
    return makeTask.GetCache();
}

PSerialDevice TRPCConfigSaveTest::FindPolledDevice(const std::string& deviceId)
{
    auto config = Core->GetConfig();
    if (!config) {
        return nullptr;
    }
    for (const auto& portConfig: config->PortConfigs) {
        for (const auto& device: portConfig->Devices) {
            if (device->Device->DeviceConfig()->Id == deviceId) {
                return device->Device;
            }
        }
    }
    return nullptr;
}

bool TRPCConfigSaveTest::HasMqttDevice(const std::string& deviceId)
{
    return Driver->BeginTx()->GetDevice(deviceId) != nullptr;
}

class TRPCConfigSaveWithoutConfigTest: public TRPCConfigSaveTest
{
protected:
    Json::Value InitialConfig() override
    {
        return Json::Value();
    }
};

//! config/Save waits for the accepted port requests until the test releases them
class TRPCConfigSaveHeldTest: public TRPCConfigSaveTest
{
protected:
    TRPCConfigSaveHeldTest()
    {
        PortTasksWaitTimeout = WAIT_TIMEOUT;
    }
};

class TRPCConfigSaveBrokenStartTest: public TRPCConfigSaveTest
{
protected:
    TRPCConfigSaveBrokenStartTest()
    {
        BreakInitialConfig = true;
    }
};

class TRPCConfigSaveDebugFromCommandLineTest: public TRPCConfigSaveTest
{
protected:
    TRPCConfigSaveDebugFromCommandLineTest()
    {
        DebugFromCommandLine = true;
    }
};

TEST_F(TRPCConfigSaveTest, InvalidConfigIsRejected)
{
    auto fileBefore = ReadConfigFile();
    auto configBefore = Core->GetConfig();
    auto deviceBefore = FindPolledDevice(DEVICE1);

    auto config = InitialConfig();
    config["ports"][0]["devices"][0]["channels"][0]["reg_type"] = "no_such_type";
    auto error = Save(config);
    EXPECT_EQ(0u, error.rfind(CONFIG_INVALID_ERROR + ": ", 0)) << error;
    EXPECT_NE(std::string::npos, error.find("reg_type")) << error;

    EXPECT_EQ(fileBefore, ReadConfigFile());
    EXPECT_EQ(configBefore, Core->GetConfig());
    EXPECT_EQ(deviceBefore, FindPolledDevice(DEVICE1));
}

TEST_F(TRPCConfigSaveTest, RequestWithoutConfigIsRejected)
{
    auto fileBefore = ReadConfigFile();
    auto error = CallAsync("config/Save", Json::Value(Json::objectValue))->Get();
    EXPECT_NE(std::string::npos, error.find("config")) << error;
    EXPECT_EQ(fileBefore, ReadConfigFile());
}

TEST_F(TRPCConfigSaveTest, ValidConfigIsSavedAndPollingRestarted)
{
    auto configBefore = Core->GetConfig();
    auto deviceBefore = FindPolledDevice(DEVICE1);
    auto config = MakeConfig({MakePort(PORT1, {DEVICE1}), MakePort(PORT2, {DEVICE2})});

    EXPECT_EQ("", Save(config));

    EXPECT_EQ(SerializeJson(config), SerializeJson(ParseJson(ReadConfigFile())));
    EXPECT_NE(configBefore, Core->GetConfig());
    auto device = FindPolledDevice(DEVICE1);
    ASSERT_TRUE(device);
    EXPECT_NE(deviceBefore, device);
    EXPECT_TRUE(FindPolledDevice(DEVICE2));
    EXPECT_TRUE(HasMqttDevice(DEVICE2));

    Json::Value empty(Json::objectValue);
    auto ports = Rpc->Call("ports/List", empty)["ports"];
    ASSERT_EQ(2u, ports.size());
    EXPECT_EQ(PORT2, ports[1]["path"].asString());
    EXPECT_EQ(SerializeJson(config), SerializeJson(Rpc->Call("config/Load", empty)["config"]));

    auto task = SubmitTask(DEVICE2);
    ASSERT_TRUE(task->WaitDone(WAIT_TIMEOUT));
    auto polledDevices = task->GetPolledDevices();
    EXPECT_NE(polledDevices.end(), std::find(polledDevices.begin(), polledDevices.end(), FindPolledDevice(DEVICE2)));
}

TEST_F(TRPCConfigSaveTest, SaveKeepsSymlinkedConfig)
{
    auto targetPath = TestDir / "data" / "wb-mqtt-serial.conf";
    std::filesystem::create_directories(targetPath.parent_path());
    std::filesystem::rename(ConfigPath, targetPath);
    std::filesystem::create_symlink(targetPath, ConfigPath);
    auto config = MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})});

    EXPECT_EQ("", Save(config));

    EXPECT_TRUE(std::filesystem::is_symlink(ConfigPath));
    EXPECT_EQ(targetPath, std::filesystem::read_symlink(ConfigPath));
    EXPECT_EQ(SerializeJson(config), SerializeJson(ParseJson(ReadFile(targetPath.string()))));
}

TEST_F(TRPCConfigSaveTest, DebugLevelIsAppliedAfterSave)
{
    EXPECT_EQ("", Save(MakeConfig({MakePort(PORT1, {DEVICE1})}, true)));
    EXPECT_TRUE(::Debug.IsEnabled());
    EXPECT_EQ("", Save(MakeConfig({MakePort(PORT1, {DEVICE1})}, false)));
    EXPECT_FALSE(::Debug.IsEnabled());
}

TEST_F(TRPCConfigSaveDebugFromCommandLineTest, DebugFromCommandLineIsKept)
{
    EXPECT_EQ("", Save(MakeConfig({MakePort(PORT1, {DEVICE1})}, false)));
    EXPECT_TRUE(::Debug.IsEnabled());
}

TEST_F(TRPCConfigSaveTest, ParametersCacheIsClearedBySave)
{
    // No device of the configurations has the id, so it is not removed on a disconnection
    const std::string id = PORT2 + ":7";
    auto cacheBefore = GetParametersCache();
    ASSERT_TRUE(cacheBefore);
    cacheBefore->Add(id, Json::Value(Json::objectValue));

    EXPECT_EQ("", Save(InitialConfig()));
    auto cache = GetParametersCache();
    ASSERT_TRUE(cache);
    EXPECT_FALSE(cache->Contains(id));
}

TEST_F(TRPCConfigSaveHeldTest, RequestsAreRefusedDuringSave)
{
    auto blocking = RunBlockingTask(DEVICE1);
    auto save = StartSave(InitialConfig());
    WaitGateClosed();

    Json::Value deviceRequest;
    deviceRequest["device_id"] = DEVICE1;
    Json::Value probeRequest = MakeSerialPortRequest(PORT1);
    probeRequest["slave_id"] = 1;
    Json::Value portLoadRequest = MakeSerialPortRequest(PORT1);
    portLoadRequest["msg"] = "01";
    portLoadRequest["response_size"] = 1;
    Json::Value setupRequest = MakeSerialPortRequest(PORT1);
    setupRequest["items"] = Json::Value(Json::arrayValue);
    std::vector<std::pair<std::string, Json::Value>> portRequests = {
        {"device/Load", deviceRequest},
        {"device/Set", deviceRequest},
        {"device/LoadConfig", deviceRequest},
        {"device/Probe", probeRequest},
        {"port/Load", portLoadRequest},
        {"port/Setup", setupRequest},
        {"port/Scan", MakeSerialPortRequest(PORT1)},
        {"fw-update/GetFirmwareInfo", MakeFwUpdateRequest()},
        {"fw-update/Update", MakeFwUpdateRequest()},
        {"fw-update/Restore", MakeFwUpdateRequest()},
        {"config/Save", []() {
             Json::Value request;
             request["config"] = MakeConfig({MakePort(PORT1, {DEVICE1})});
             return request;
         }()}};
    for (const auto& request: portRequests) {
        auto answer = CallAsync(request.first, request.second);
        // Refused at once
        ASSERT_TRUE(answer->IsAnswered()) << request.first;
        EXPECT_EQ(CONFIG_BUSY_ERROR, answer->Get()) << request.first;
    }

    Json::Value empty(Json::objectValue);
    EXPECT_EQ(CONFIG_BUSY_ERROR, CallError("config/Load", empty));
    Json::Value schemaRequest;
    schemaRequest["type"] = "MSU34";
    EXPECT_EQ(CONFIG_BUSY_ERROR, CallError("config/GetSchema", schemaRequest));
    Json::Value uploadRequest;
    uploadRequest["content"] = "{}";
    uploadRequest["filename"] = "device.json";
    EXPECT_EQ(CONFIG_BUSY_ERROR, CallError("templates/Upload", uploadRequest));
    Json::Value deleteRequest;
    deleteRequest["type"] = "MSU34";
    EXPECT_EQ(CONFIG_BUSY_ERROR, CallError("templates/Delete", deleteRequest));
    Json::Value setPollRequest;
    setPollRequest["device_id"] = DEVICE1;
    setPollRequest["poll"] = false;
    EXPECT_EQ(CONFIG_BUSY_ERROR, CallError("device/SetPoll", setPollRequest));

    // The methods which touch neither the ports nor the configuration are answered at once
    EXPECT_EQ(1u, Rpc->Call("ports/List", empty)["ports"].size());
    EXPECT_EQ(1u, Rpc->Call("ports/Load", empty).size());
    Json::Value clearErrorRequest;
    clearErrorRequest["slave_id"] = 1;
    clearErrorRequest["port"]["path"] = PORT1;
    EXPECT_EQ("Ok", Rpc->Call("fw-update/ClearError", clearErrorRequest).asString());

    EXPECT_FALSE(save->IsAnswered());
    blocking->Release();
    EXPECT_EQ("", save->Get());
    EXPECT_EQ("", CallError("config/Load", empty));
}

TEST_F(TRPCConfigSaveHeldTest, QueuedRequestsAreCancelled)
{
    auto blocking = RunBlockingTask(DEVICE1);
    auto queued = SubmitTask(DEVICE1);
    auto queuedRequest = CallAsync("port/Scan", MakeSerialPortRequest(PORT1));

    auto save = StartSave(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})}));
    WaitCancelRequested(queued);
    EXPECT_FALSE(save->IsAnswered());

    // The cancelled requests end as soon as the port gets to them
    blocking->Release();
    EXPECT_EQ(REQUEST_CANCELLED_ERROR, queuedRequest->Get());
    ASSERT_TRUE(queued->WaitCancelled(WAIT_TIMEOUT));
    EXPECT_EQ("", save->Get());
    EXPECT_EQ(0, queued->GetRuns());
    EXPECT_TRUE(FindPolledDevice(DEVICE2));
}

TEST_F(TRPCConfigSaveHeldTest, QueuedRequestOfPortOutsideConfigIsCancelled)
{
    TPortSettings portSettings{TSerialPortSettings(PORT3)};
    auto blocking = std::make_shared<TBlockingTask>();
    BlockingTasks.push_back(blocking);
    TaskRunner->RunTask(portSettings, blocking);
    ASSERT_TRUE(blocking->WaitStarted(WAIT_TIMEOUT));
    auto queued = SubmitTask(portSettings);

    auto save = StartSave(MakeConfig({MakePort(PORT1, {DEVICE1}), MakePort(PORT3, {DEVICE3})}));
    WaitCancelRequested(queued);
    EXPECT_FALSE(save->IsAnswered());
    blocking->Release();
    ASSERT_TRUE(queued->WaitCancelled(WAIT_TIMEOUT));
    EXPECT_EQ("", save->Get());
    EXPECT_EQ(0, queued->GetRuns());
}

TEST_F(TRPCConfigSaveTest, SaveGetsPortBusyIfRequestDoesNotEnd)
{
    auto fileBefore = ReadConfigFile();
    auto configBefore = Core->GetConfig();
    auto deviceBefore = FindPolledDevice(DEVICE1);
    auto blocking = RunBlockingTask(DEVICE1);

    EXPECT_EQ(PORT_BUSY_ERROR, Save(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})})));

    EXPECT_EQ(fileBefore, ReadConfigFile());
    EXPECT_EQ(configBefore, Core->GetConfig());
    EXPECT_EQ(deviceBefore, FindPolledDevice(DEVICE1));
    EXPECT_FALSE(FindPolledDevice(DEVICE2));
    EXPECT_FALSE(HasMqttDevice(DEVICE2));

    // The requests are accepted again
    auto request = SubmitTask(DEVICE1);
    EXPECT_EQ("", CallError("config/Load"));
    blocking->Release();
    ASSERT_TRUE(request->WaitDone(WAIT_TIMEOUT));
    auto polledDevices = request->GetPolledDevices();
    EXPECT_NE(polledDevices.end(), std::find(polledDevices.begin(), polledDevices.end(), deviceBefore));
}

TEST_F(TRPCConfigSaveTest, WriteFailureChangesNothing)
{
    // A directory in place of the temporary file can not be opened for writing
    std::filesystem::create_directories(ConfigPath + ".tmp/dir");
    auto filesBefore = ListDir(TestDir);
    auto fileBefore = ReadConfigFile();
    auto configBefore = Core->GetConfig();
    auto deviceBefore = FindPolledDevice(DEVICE1);

    EXPECT_EQ(WRITE_FAILED_ERROR, Save(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})}, true)));

    EXPECT_EQ(fileBefore, ReadConfigFile());
    EXPECT_EQ(filesBefore, ListDir(TestDir));
    EXPECT_EQ(configBefore, Core->GetConfig());
    EXPECT_EQ(deviceBefore, FindPolledDevice(DEVICE1));
    EXPECT_TRUE(HasMqttDevice(DEVICE1));
    EXPECT_FALSE(HasMqttDevice(DEVICE2));
    EXPECT_EQ(DebugWasEnabled, ::Debug.IsEnabled());

    auto request = SubmitTask(DEVICE1);
    ASSERT_TRUE(request->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ("", CallError("config/Load"));
}

//! An update waiting in the queue of the port is cancelled
TEST_F(TRPCConfigSaveHeldTest, QueuedUpdateIsCancelled)
{
    for (const auto& method: {"fw-update/Update", "fw-update/Restore"}) {
        auto blocking = OccupyPort(PORT1);
        auto update = CallAsync(method, MakeFwUpdateRequest());
        auto queued = SubmitTask(TPortSettings{TSerialPortSettings(PORT1)});

        auto save = StartSave(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})}));
        WaitCancelRequested(queued);
        blocking->Release();
        EXPECT_EQ(REQUEST_CANCELLED_ERROR, update->Get()) << method;
        EXPECT_EQ("", save->Get()) << method;

        // The cancelled update does not keep the other updates out, this one fails as nothing is connected
        auto next = CallAsync(method, MakeFwUpdateRequest())->Get();
        EXPECT_NE("", next) << method;
        EXPECT_NE(CONFIG_BUSY_ERROR, next) << method;
        EXPECT_NE(REQUEST_CANCELLED_ERROR, next) << method;
        EXPECT_EQ(std::string::npos, next.find("already executing")) << method << ": " << next;
    }
}

TEST_F(TRPCConfigSaveHeldTest, SaveAndFwUpdateStartedTogether)
{
    // An accepted update waits in the queue of the port, an accepted saving waits for the port to stop
    auto blocking = OccupyPort(PORT1);
    auto queued = SubmitTask(TPortSettings{TSerialPortSettings(PORT1)});

    std::promise<void> start;
    std::shared_future<void> started = start.get_future().share();
    auto saveThread = std::async(std::launch::async, [&]() {
        started.wait();
        return StartSave(InitialConfig());
    });
    auto updateThread = std::async(std::launch::async, [&]() {
        started.wait();
        return CallAsync("fw-update/Update", MakeFwUpdateRequest());
    });
    start.set_value();
    auto save = saveThread.get();
    auto update = updateThread.get();
    WaitCancelRequested(queued);
    blocking->Release();

    // The update is refused or cancelled, whichever comes first
    EXPECT_EQ("", save->Get());
    auto updateError = update->Get();
    EXPECT_TRUE(updateError == CONFIG_BUSY_ERROR || updateError == REQUEST_CANCELLED_ERROR) << updateError;
}

TEST_F(TRPCConfigSaveTest, PortOutsideConfigIsPolledAfterSave)
{
    TPortSettings port3{TSerialPortSettings(PORT3)};
    auto beforeSave = SubmitTask(port3);
    ASSERT_TRUE(beforeSave->WaitDone(WAIT_TIMEOUT));
    EXPECT_TRUE(beforeSave->GetPolledDevices().empty());

    EXPECT_EQ("", Save(MakeConfig({MakePort(PORT3, {DEVICE3})})));

    auto byPort = SubmitTask(port3);
    ASSERT_TRUE(byPort->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ(PORT3, byPort->GetPortPath());
    auto polledDevices = byPort->GetPolledDevices();
    ASSERT_EQ(1u, polledDevices.size());
    EXPECT_EQ(FindPolledDevice(DEVICE3), polledDevices.front());

    // The port removed from the configuration is still available
    EXPECT_FALSE(FindPolledDevice(DEVICE1));
    auto removedPort = SubmitTask(TPortSettings{TSerialPortSettings(PORT1)});
    ASSERT_TRUE(removedPort->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ(PORT1, removedPort->GetPortPath());
    EXPECT_TRUE(removedPort->GetPolledDevices().empty());
}

TEST_F(TRPCConfigSaveTest, PollingRestartFailure)
{
    ConfigLoader->BreakLoadedConfig = true;
    auto config = MakeConfig({MakePort(PORT1, {DEVICE1})});

    EXPECT_EQ(POLLING_RESTART_FAILED_ERROR, Save(config));

    EXPECT_EQ(SerializeJson(config), SerializeJson(ParseJson(ReadConfigFile())));
    EXPECT_FALSE(Core->GetConfig());
    EXPECT_FALSE(FindPolledDevice(DEVICE1));
    EXPECT_FALSE(HasMqttDevice(DEVICE1));
    Json::Value empty(Json::objectValue);
    EXPECT_EQ(0u, Rpc->Call("ports/List", empty)["ports"].size());
    EXPECT_EQ("", CallError("config/Load", empty));
    auto request = SubmitTask(TPortSettings{TSerialPortSettings(PORT1)});
    ASSERT_TRUE(request->WaitDone(WAIT_TIMEOUT));
    EXPECT_TRUE(request->GetPolledDevices().empty());

    ConfigLoader->BreakLoadedConfig = false;
    EXPECT_EQ("", Save(config));
    EXPECT_TRUE(FindPolledDevice(DEVICE1));
}

TEST_F(TRPCConfigSaveBrokenStartTest, ServiceStartsWithoutPolling)
{
    EXPECT_FALSE(Core->GetConfig());
    EXPECT_FALSE(FindPolledDevice(DEVICE1));
    EXPECT_FALSE(HasMqttDevice(DEVICE1));
    EXPECT_EQ("", CallError("config/Load"));

    EXPECT_EQ("", Save(InitialConfig()));
    EXPECT_TRUE(FindPolledDevice(DEVICE1));
}

//! Each handler would pass a value written to a control to the device once more
TEST_F(TRPCConfigSaveTest, ControlWriteIsHandledOnceAfterRestarts)
{
    EXPECT_EQ(1u, CountingDriver->GetControlOnValueHandlersCount());
    EXPECT_EQ("", Save(InitialConfig()));
    EXPECT_EQ("", Save(InitialConfig()));
    EXPECT_EQ("", Save(InitialConfig()));
    EXPECT_EQ(1u, CountingDriver->GetControlOnValueHandlersCount());
}

//! The file is written when the port clients have stopped
TEST_F(TRPCConfigSaveHeldTest, FileIsWrittenAfterClientsStop)
{
    auto blocking = OccupyPort(PORT1);
    auto queued = SubmitTask(TPortSettings{TSerialPortSettings(PORT1)});
    auto fileBefore = ReadConfigFile();
    auto config = MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})});
    auto save = StartSave(config);
    WaitCancelRequested(queued);

    EXPECT_EQ(fileBefore, ReadConfigFile());
    EXPECT_EQ(1u, CountingDriver->GetControlOnValueHandlersCount());

    blocking->Release();
    EXPECT_EQ("", save->Get());
    EXPECT_EQ(SerializeJson(config), SerializeJson(ParseJson(ReadConfigFile())));
    EXPECT_EQ(1u, CountingDriver->GetControlOnValueHandlersCount());
}

TEST_F(TRPCConfigSaveTest, SuspendedDeviceIsPolledAfterSave)
{
    // A request run on the port proves the polling cycle is going, so the poll can be suspended
    ASSERT_TRUE(SubmitTask(DEVICE1)->WaitDone(WAIT_TIMEOUT));
    Json::Value setPollRequest;
    setPollRequest["device_id"] = DEVICE1;
    setPollRequest["poll"] = false;
    EXPECT_EQ("", CallError("device/SetPoll", setPollRequest));

    EXPECT_EQ("", Save(InitialConfig()));

    ASSERT_TRUE(SubmitTask(DEVICE1)->WaitDone(WAIT_TIMEOUT));
    setPollRequest["poll"] = true;
    auto error = CallError("device/SetPoll", setPollRequest);
    EXPECT_NE(std::string::npos, error.find("is not suspended")) << error;
}

TEST_F(TRPCConfigSaveWithoutConfigTest, SaveStartsPolling)
{
    Json::Value empty(Json::objectValue);
    EXPECT_FALSE(Core->GetConfig());
    EXPECT_EQ(0u, Rpc->Call("ports/List", empty)["ports"].size());

    EXPECT_EQ("", Save(MakeConfig({MakePort(PORT1, {DEVICE1})})));

    EXPECT_TRUE(Core->GetConfig());
    EXPECT_TRUE(HasMqttDevice(DEVICE1));
    auto request = SubmitTask(DEVICE1);
    ASSERT_TRUE(request->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ(1u, request->GetPolledDevices().size());
    EXPECT_EQ(1u, Rpc->Call("ports/List", empty)["ports"].size());
}

TEST_F(TRPCConfigSaveHeldTest, StopWaitsForSave)
{
    auto blocking = RunBlockingTask(DEVICE1);
    auto config = MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})});
    auto save = StartSave(config);
    WaitGateClosed();

    auto stop = std::async(std::launch::async, [this]() { Core->Stop(); });
    EXPECT_EQ(std::future_status::timeout, stop.wait_for(200ms));

    blocking->Release();
    EXPECT_EQ("", save->Get());
    ASSERT_EQ(std::future_status::ready, stop.wait_for(WAIT_TIMEOUT));

    // The configuration is written, but the polling with it is not started
    EXPECT_EQ(SerializeJson(config), SerializeJson(ParseJson(ReadConfigFile())));
    EXPECT_FALSE(Core->GetConfig());
    EXPECT_FALSE(HasMqttDevice(DEVICE1));
    EXPECT_FALSE(HasMqttDevice(DEVICE2));
    EXPECT_TRUE(Core->IsBusy());
    EXPECT_EQ(CONFIG_BUSY_ERROR, Save(InitialConfig()));
}

TEST_F(TRPCConfigSaveHeldTest, StopCancelsQueuedTasksOfPortOutsideConfig)
{
    TPortSettings portSettings{TSerialPortSettings(PORT3)};
    auto blocking = std::make_shared<TBlockingTask>();
    BlockingTasks.push_back(blocking);
    TaskRunner->RunTask(portSettings, blocking);
    ASSERT_TRUE(blocking->WaitStarted(WAIT_TIMEOUT));
    auto queued = SubmitTask(portSettings);

    // The executor is stopped before Stop returns, so its running task ends first
    auto stop = std::async(std::launch::async, [this]() { Core->Stop(); });
    WaitCancelRequested(queued);
    EXPECT_EQ(std::future_status::timeout, stop.wait_for(200ms));
    blocking->Release();
    ASSERT_EQ(std::future_status::ready, stop.wait_for(WAIT_TIMEOUT));
    EXPECT_TRUE(queued->WaitCancelled(0ms));
    EXPECT_EQ(0, queued->GetRuns());
}

TEST_F(TRPCConfigSaveTest, PortOutsideConfigAcceptsRequestsAfterRefusedSave)
{
    TPortSettings port3{TSerialPortSettings(PORT3)};
    auto blocking = std::make_shared<TBlockingTask>();
    BlockingTasks.push_back(blocking);
    TaskRunner->RunTask(port3, blocking);
    ASSERT_TRUE(blocking->WaitStarted(WAIT_TIMEOUT));

    EXPECT_EQ(PORT_BUSY_ERROR, Save(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})})));
    blocking->Release();
    auto afterPortBusy = SubmitTask(port3);
    ASSERT_TRUE(afterPortBusy->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ(1, afterPortBusy->GetRuns());

    // A directory in place of the temporary file can not be opened for writing
    std::filesystem::create_directories(ConfigPath + ".tmp/dir");
    EXPECT_EQ(WRITE_FAILED_ERROR, Save(MakeConfig({MakePort(PORT1, {DEVICE1, DEVICE2})})));
    auto afterWriteFailed = SubmitTask(port3);
    ASSERT_TRUE(afterWriteFailed->WaitDone(WAIT_TIMEOUT));
    EXPECT_EQ(1, afterWriteFailed->GetRuns());
}
