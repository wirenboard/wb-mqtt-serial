#pragma once

#include "confed_device_schemas_map.h"
#include "confed_protocol_schemas_map.h"
#include "device_parameters_cache.h"
#include "files_watcher.h"
#include "port_task_dispatcher.h"
#include "template_change.h"

//! How long a configuration change waits for the port tasks
const std::chrono::seconds DefaultPortTasksWaitTimeout(10);

//! A configuration or template change is running or the service is stopped
class TSerialDriverCoreBusyError: public std::runtime_error
{
public:
    TSerialDriverCoreBusyError();
};

class TApplyConfigError: public std::runtime_error
{
public:
    enum class TReason
    {
        //! Another configuration is being applied or the service is stopped
        Busy,
        //! The message is the validation error
        ConfigInvalid,
        PortTasksTimeout,
        WriteFailed,
        //! The configuration is written, nothing is polled
        PollingRestartFailed,
        //! The type is used and the change is not forced
        TemplateInUse,
        //! TemplateInUse with problems
        ApplyProblems
    };

    TApplyConfigError(TReason reason, const std::string& message);
    //! ApplyProblems
    TApplyConfigError(const std::string& message, const TApplyProblems& problems);

    TReason GetReason() const;
    const TApplyProblems& GetProblems() const;

private:
    TReason Reason;
    TApplyProblems Problems;
};

struct TTemplateApplyResult
{
    //! Null if the type has no template
    PDeviceTemplate Template;
    //! As config/Load gives it, null if it is not read
    Json::Value Config;
};

struct TSerialDriverCoreFiles
{
    std::string Config;
    std::string TemplatesDir;
    std::string UserTemplatesDir;
    std::string TemplatesSchema;
    std::string CommonDeviceSchema;
    std::string PortsSchema;
    std::string ProtocolSchemasDir;
};

/**
 * @brief Owns the configuration, the templates and everything that accesses the ports: the driver polling
 *        the configuration and the executors of the ports outside it
 */
class TSerialDriverCore: public util::TNonCopyable
{
public:
    /**
     * @brief Loads the templates and the configuration. If the configuration can not be loaded or polled,
     *        the service starts without polling
     *
     * @param debugFromCommandLine debug logging stays enabled whatever "debug" of the configuration is
     * @param portTasksWaitTimeout how long a configuration or template change waits for the port tasks
     *
     * @throws std::invalid_argument if mqttDriver is null, std::exception if the schemas can not be loaded
     */
    TSerialDriverCore(WBMQTT::PDeviceDriver mqttDriver,
                      const TSerialDriverCoreFiles& files,
                      bool debugFromCommandLine,
                      TFeaturePortFactoryFn portFactory = DefaultPortFactory,
                      std::chrono::milliseconds portTasksWaitTimeout = DefaultPortTasksWaitTimeout);
    ~TSerialDriverCore();

    //! A configuration or template change is running or the service is stopped
    bool IsBusy();

    const std::string& GetConfigPath() const;
    const TSerialDeviceFactory& GetDeviceFactory() const;
    const Json::Value& GetPortsSchema() const;
    TProtocolConfedSchemasMap& GetProtocolConfedSchemas();
    PTemplateMap GetTemplates();
    TDevicesConfedSchemasMap& GetDeviceConfedSchemas();

    /**
     * @brief Adds the task made by makeTask to the client polling the target or to the executor
     *        of the target port
     *
     * @throws TSerialDriverCoreBusyError, TTaskTargetNotFoundError, the exceptions of makeTask
     */
    void AddTask(const TTaskTarget& target, const TMakeTaskFn& makeTask);

    //! Suspends or resumes the polling of the config device of the target
    //! @throws TSerialDriverCoreBusyError, TTaskTargetNotFoundError, std::runtime_error of the client
    void SetPoll(const TTaskTarget& target, bool poll);

    //! The configuration the polling runs with, null if nothing is polled
    PHandlerConfig GetConfig();

    void Start();

    //! Waits for ApplyConfig, stops the polling and the executors, the ports are not accessed after it
    void Stop();

    /**
     * @brief Thread-safe. Checks the configuration, stops the port clients, writes the configuration and restarts
     *        the polling with it. If the service is stopped meanwhile, the polling is not restarted
     *
     * @throws TApplyConfigError. The file, the polled configuration and the MQTT devices are kept
     *         unless the reason is PollingRestartFailed. After a successful check the queued port tasks
     *         are cancelled whatever the result
     */
    void ApplyConfig(const Json::Value& config);

    /**
     * @brief Thread-safe. If the type is used, its devices are read by the new template if it has "hw". Without force
     *        nothing is changed then, with force the configuration gets the read parameters and the polling
     *        is restarted as ApplyConfig does
     *
     * @throws TApplyConfigError, std::exception if the template is invalid
     */
    TTemplateApplyResult UploadTemplate(const std::string& content, const std::string& fileName, bool force);

    //! Thread-safe. Same as UploadTemplate for the remaining template of the type
    //! @throws TApplyConfigError, std::exception if the type has no user template
    TTemplateApplyResult DeleteTemplate(const std::string& deviceType, bool force);

private:
    WBMQTT::PDeviceDriver MqttDriver;
    std::string ConfigPath;
    TSerialDeviceFactory DeviceFactory;
    Json::Value CommonDeviceSchema;
    Json::Value PortsSchema;
    TProtocolConfedSchemasMap ProtocolConfedSchemas;
    TConfigLoadParams ConfigLoadParams;
    PTemplateMap Templates;
    TDevicesConfedSchemasMap DeviceConfedSchemas;
    bool DebugFromCommandLine;
    std::chrono::milliseconds PortTasksWaitTimeout;

    std::mutex ApplyConfigMutex;

    std::mutex Mutex;
    bool Applying;
    bool Stopped;
    PHandlerConfig Config;
    TPortTaskDispatcher PortTasks;

    //! Changes the templates, so it is stopped before they are destroyed
    std::unique_ptr<TFilesWatcher> TemplatesWatcher;

    //! The mutex is held by the caller
    bool IsBusyLocked() const;
    void ThrowIfBusy() const;
    void HandleTemplateChangeEvent(const std::string& fileName, TFilesWatcher::TEvent event);

    //! @throws TApplyConfigError PortTasksTimeout
    void PausePortTasks();
    void DoApplyConfig(const Json::Value& config);
    void DoApplyTemplate(const TTemplateChange& change, bool force);
    //! As config/Load gives it, null if it is not read
    Json::Value LoadConfedConfig();
    //! Null handlerConfig leaves nothing polled
    //! @throws TApplyConfigError PollingRestartFailed
    void RestartPolling(PHandlerConfig handlerConfig);
    //! Null handlerConfig stops the polling
    void SwitchDriver(PHandlerConfig handlerConfig);
};
