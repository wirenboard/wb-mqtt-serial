#include "serial_driver_core.h"
#include "confed_json_generator.h"
#include "file_utils.h"
#include "log.h"
#include <exception>

#define LOG(logger) ::logger.Log() << "[serial] "

namespace
{
    //! The data of a device is dropped when the device disconnects
    PDeviceParametersCache MakeParametersCache(PHandlerConfig handlerConfig)
    {
        auto cache = std::make_shared<TDeviceParametersCache>();
        if (!handlerConfig) {
            return cache;
        }
        for (const auto& portConfig: handlerConfig->PortConfigs) {
            for (const auto& device: portConfig->Devices) {
                std::string id = cache->GetId(*portConfig->Port, device->Device->DeviceConfig()->SlaveId);
                device->Device->AddOnConnectionStateChangedCallback([cache, id](PSerialDevice device) {
                    if (device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) {
                        cache->Remove(id);
                    }
                });
            }
        }
        return cache;
    }

    PTemplateMap LoadTemplates(const TSerialDriverCoreFiles& files, const Json::Value& commonDeviceSchema)
    {
        auto templates =
            std::make_shared<TTemplateMap>(LoadConfigTemplatesSchema(files.TemplatesSchema, commonDeviceSchema),
                                           files.UserTemplatesDir);
        templates->AddTemplatesDir(files.TemplatesDir);
        // templates/Upload creates the directory
        try {
            templates->AddTemplatesDir(files.UserTemplatesDir);
        } catch (const TNoDirError& e) {
            LOG(Warn) << "No user templates are loaded: " << e.what();
        }
        return templates;
    }
}

TSerialDriverCoreBusyError::TSerialDriverCoreBusyError()
    : std::runtime_error("a configuration or template change is running or the service is stopped")
{}

TApplyConfigError::TApplyConfigError(TReason reason, const std::string& message)
    : std::runtime_error(message),
      Reason(reason)
{}

TApplyConfigError::TApplyConfigError(const std::string& message, const TApplyProblems& problems)
    : std::runtime_error(message),
      Reason(TReason::ApplyProblems),
      Problems(problems)
{}

TApplyConfigError::TReason TApplyConfigError::GetReason() const
{
    return Reason;
}

const TApplyProblems& TApplyConfigError::GetProblems() const
{
    return Problems;
}

TSerialDriverCore::TSerialDriverCore(WBMQTT::PDeviceDriver mqttDriver,
                                     const TSerialDriverCoreFiles& files,
                                     bool debugFromCommandLine,
                                     TFeaturePortFactoryFn portFactory,
                                     std::chrono::milliseconds portTasksWaitTimeout)
    : MqttDriver(mqttDriver),
      ConfigPath(files.Config),
      CommonDeviceSchema(WBMQTT::JSON::Parse(files.CommonDeviceSchema)),
      PortsSchema(WBMQTT::JSON::Parse(files.PortsSchema)),
      ProtocolConfedSchemas(files.ProtocolSchemasDir, CommonDeviceSchema),
      ConfigLoadParams{DeviceFactory, CommonDeviceSchema, PortsSchema, ProtocolConfedSchemas, portFactory},
      Templates(LoadTemplates(files, CommonDeviceSchema)),
      DeviceConfedSchemas(*Templates, DeviceFactory, CommonDeviceSchema),
      DebugFromCommandLine(debugFromCommandLine),
      PortTasksWaitTimeout(portTasksWaitTimeout),
      Applying(false),
      Stopped(false),
      PortTasks(portFactory)
{
    if (!MqttDriver) {
        throw std::invalid_argument("no MQTT driver to publish the devices");
    }
    RegisterProtocols(DeviceFactory);
    // The service goes on without the polling, config/Save can fix the configuration
    PHandlerConfig handlerConfig;
    try {
        handlerConfig = ::LoadConfig(ConfigPath,
                                     DeviceFactory,
                                     CommonDeviceSchema,
                                     *Templates,
                                     PortsSchema,
                                     ProtocolConfedSchemas,
                                     ConfigLoadParams.PortFactory);
    } catch (const std::exception& e) {
        LOG(Error) << e.what();
    }
    PMQTTSerialDriver serialDriver;
    if (handlerConfig) {
        if (handlerConfig->Debug) {
            ::Debug.SetEnabled(true);
        }
        try {
            serialDriver = std::make_shared<TMQTTSerialDriver>(MqttDriver, handlerConfig);
            Config = handlerConfig;
        } catch (const std::exception& e) {
            LOG(Error) << "the polling is not started: " << e.what();
        }
    }
    PortTasks.Switch(serialDriver, MakeParametersCache(Config));
    TemplatesWatcher = std::make_unique<TFilesWatcher>(
        std::vector<std::string>{files.TemplatesDir, files.UserTemplatesDir},
        [this](const std::string& fileName, TFilesWatcher::TEvent event) {
            HandleTemplateChangeEvent(fileName, event);
        });
}

TSerialDriverCore::~TSerialDriverCore()
{
    TemplatesWatcher.reset();
}

const std::string& TSerialDriverCore::GetConfigPath() const
{
    return ConfigPath;
}

const TSerialDeviceFactory& TSerialDriverCore::GetDeviceFactory() const
{
    return DeviceFactory;
}

const Json::Value& TSerialDriverCore::GetPortsSchema() const
{
    return PortsSchema;
}

TProtocolConfedSchemasMap& TSerialDriverCore::GetProtocolConfedSchemas()
{
    return ProtocolConfedSchemas;
}

PTemplateMap TSerialDriverCore::GetTemplates()
{
    return Templates;
}

TDevicesConfedSchemasMap& TSerialDriverCore::GetDeviceConfedSchemas()
{
    return DeviceConfedSchemas;
}

void TSerialDriverCore::HandleTemplateChangeEvent(const std::string& fileName, TFilesWatcher::TEvent event)
{
    if (event == TFilesWatcher::TEvent::CloseWrite) {
        LOG(Debug) << fileName << " changed. Reloading template";
        try {
            auto updatedTypes = Templates->UpdateTemplate(fileName);
            for (const auto& deviceType: updatedTypes) {
                DeviceConfedSchemas.InvalidateCache(deviceType);
            }
        } catch (const std::exception& e) {
            LOG(Error) << "Failed to reload template: " << e.what();
        }
        return;
    }
    if (event == TFilesWatcher::TEvent::Delete) {
        LOG(Debug) << fileName << " deleted";
        DeviceConfedSchemas.InvalidateCache(Templates->DeleteTemplate(fileName));
    }
}

bool TSerialDriverCore::IsBusyLocked() const
{
    return Applying || Stopped;
}

void TSerialDriverCore::ThrowIfBusy() const
{
    if (IsBusyLocked()) {
        throw TSerialDriverCoreBusyError();
    }
}

bool TSerialDriverCore::IsBusy()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return IsBusyLocked();
}

void TSerialDriverCore::SetPoll(const TTaskTarget& target, bool poll)
{
    std::lock_guard<std::mutex> lock(Mutex);
    ThrowIfBusy();
    PortTasks.SetPoll(target, poll);
}

void TSerialDriverCore::AddTask(const TTaskTarget& target, const TMakeTaskFn& makeTask)
{
    std::lock_guard<std::mutex> lock(Mutex);
    ThrowIfBusy();
    PortTasks.AddTask(target, makeTask);
}

PHandlerConfig TSerialDriverCore::GetConfig()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Config;
}

void TSerialDriverCore::Start()
{
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    PortTasks.Start();
}

void TSerialDriverCore::Stop()
{
    {
        // ApplyConfig, if it runs, does not start a new polling then
        std::lock_guard<std::mutex> lock(Mutex);
        Stopped = true;
    }
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    PortTasks.Switch(nullptr, MakeParametersCache(nullptr));
}

void TSerialDriverCore::ApplyConfig(const Json::Value& config)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (IsBusyLocked()) {
            throw TApplyConfigError(TApplyConfigError::TReason::Busy, TSerialDriverCoreBusyError().what());
        }
        Applying = true;
    }
    try {
        DoApplyConfig(config);
    } catch (...) {
        std::lock_guard<std::mutex> lock(Mutex);
        Applying = false;
        throw;
    }
    std::lock_guard<std::mutex> lock(Mutex);
    Applying = false;
}

TTemplateApplyResult TSerialDriverCore::UploadTemplate(const std::string& content,
                                                       const std::string& fileName,
                                                       bool force)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (IsBusyLocked()) {
            throw TApplyConfigError(TApplyConfigError::TReason::Busy, TSerialDriverCoreBusyError().what());
        }
        Applying = true;
    }
    TTemplateApplyResult res;
    try {
        // The paths are chosen by the templates, so the change is made while no other change runs
        auto change = MakeUploadChange(*Templates, content, fileName);
        DoApplyTemplate(change, force);
        res.Template = FindTemplate(*Templates, change.DeviceType);
        res.Config = LoadConfedConfig();
    } catch (...) {
        std::lock_guard<std::mutex> lock(Mutex);
        Applying = false;
        throw;
    }
    std::lock_guard<std::mutex> lock(Mutex);
    Applying = false;
    return res;
}

TTemplateApplyResult TSerialDriverCore::DeleteTemplate(const std::string& deviceType, bool force)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (IsBusyLocked()) {
            throw TApplyConfigError(TApplyConfigError::TReason::Busy, TSerialDriverCoreBusyError().what());
        }
        Applying = true;
    }
    TTemplateApplyResult res;
    try {
        // The path is chosen by the templates, so the change is made while no other change runs
        auto change = MakeDeleteChange(*Templates, deviceType);
        DoApplyTemplate(change, force);
        res.Template = FindTemplate(*Templates, change.DeviceType);
        res.Config = LoadConfedConfig();
    } catch (...) {
        std::lock_guard<std::mutex> lock(Mutex);
        Applying = false;
        throw;
    }
    std::lock_guard<std::mutex> lock(Mutex);
    Applying = false;
    return res;
}

void TSerialDriverCore::PausePortTasks()
{
    if (!PortTasks.Pause(PortTasksWaitTimeout)) {
        throw TApplyConfigError(TApplyConfigError::TReason::PortTasksTimeout,
                                "the port tasks did not stop in " + std::to_string(PortTasksWaitTimeout.count()) +
                                    " ms");
    }
}

void TSerialDriverCore::DoApplyConfig(const Json::Value& config)
{
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    PHandlerConfig handlerConfig;
    try {
        handlerConfig = LoadConfigJson(config, ConfigLoadParams, *Templates);
    } catch (const std::exception& e) {
        throw TApplyConfigError(TApplyConfigError::TReason::ConfigInvalid, e.what());
    }
    PausePortTasks();
    try {
        WriteConfigFile(ConfigPath, config);
    } catch (const std::exception& e) {
        PortTasks.Resume();
        throw TApplyConfigError(TApplyConfigError::TReason::WriteFailed,
                                std::string("unable to write the configuration: ") + e.what());
    }
    RestartPolling(handlerConfig);
}

void TSerialDriverCore::DoApplyTemplate(const TTemplateChange& change, bool force)
{
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    Json::Value config;
    try {
        config = WBMQTT::JSON::Parse(ConfigPath);
    } catch (const std::exception& e) {
        // Missing or broken config can't refer to the template, nothing to protect
        LOG(Debug) << "the configuration is not read: " << e.what();
    }
    auto devicesCount = CountDevicesOfType(config, change.DeviceType);
    if (devicesCount == 0) {
        try {
            WriteTemplateFile(change);
        } catch (const std::exception& e) {
            throw TApplyConfigError(TApplyConfigError::TReason::WriteFailed,
                                    std::string("unable to change the template file: ") + e.what());
        }
        CommitTemplates(change, *Templates, DeviceConfedSchemas);
        return;
    }
    auto newTemplates = MakeChangedTemplates(*Templates, change);
    auto changedConfig = MakeChangedConfig(config,
                                           change.DeviceType,
                                           ConfigLoadParams,
                                           ConfigPath,
                                           *Templates,
                                           *newTemplates,
                                           PortTasks);
    if (!force) {
        // Without a template every device fails the check, that is what template-in-use tells
        if (!changedConfig.Problems.Empty() && FindTemplate(*newTemplates, change.DeviceType)) {
            throw TApplyConfigError("some devices of \"" + change.DeviceType + "\" can not take the template as is",
                                    changedConfig.Problems);
        }
        throw TApplyConfigError(TApplyConfigError::TReason::TemplateInUse,
                                "device type \"" + change.DeviceType + "\" is used by " + std::to_string(devicesCount) +
                                    " device(s) of the configuration, \"force\" is required to change its template");
    }

    PausePortTasks();
    try {
        if (changedConfig.Config == config) {
            WriteTemplateFile(change);
        } else {
            WriteTemplateAndConfigFiles(change, ConfigPath, changedConfig.Config);
        }
    } catch (const std::exception& e) {
        PortTasks.Resume();
        throw TApplyConfigError(TApplyConfigError::TReason::WriteFailed,
                                std::string("unable to write the files: ") + e.what());
    }
    CommitTemplates(change, *Templates, DeviceConfedSchemas);
    RestartPolling(changedConfig.HandlerConfig);
}

Json::Value TSerialDriverCore::LoadConfedConfig()
{
    try {
        return MakeJsonForConfed(ConfigPath, *Templates);
    } catch (const std::exception& e) {
        LOG(Debug) << "the configuration is not read: " << e.what();
        return Json::Value();
    }
}

void TSerialDriverCore::RestartPolling(PHandlerConfig handlerConfig)
{
    if (handlerConfig) {
        ::Debug.SetEnabled(DebugFromCommandLine || handlerConfig->Debug);
    }
    try {
        SwitchDriver(handlerConfig);
    } catch (const std::exception& e) {
        throw TApplyConfigError(TApplyConfigError::TReason::PollingRestartFailed,
                                std::string("the files are written, but the polling is not started: ") + e.what());
    }
}

void TSerialDriverCore::SwitchDriver(PHandlerConfig handlerConfig)
{
    // The old configuration is reported until the new one is polled
    PortTasks.Switch(nullptr, MakeParametersCache(nullptr));

    auto parametersCache = MakeParametersCache(handlerConfig);
    PMQTTSerialDriver newDriver;
    std::exception_ptr error;
    if (handlerConfig) {
        try {
            newDriver = std::make_shared<TMQTTSerialDriver>(MqttDriver, handlerConfig);
        } catch (...) {
            error = std::current_exception();
        }
    }
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (newDriver && !Stopped) {
            // Under the mutex, so Stop does not miss the started driver
            PortTasks.Switch(newDriver, parametersCache);
            Config = handlerConfig;
            return;
        }
        Config = nullptr;
    }
    if (error) {
        std::rethrow_exception(error);
    }
    if (newDriver) {
        newDriver->ClearDevices();
    }
}
