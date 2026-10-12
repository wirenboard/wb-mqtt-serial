#pragma once

#include <optional>
#include <variant>

#include "port/port_settings.h"

#include "device_parameters_cache.h"
#include "serial_client_task_executor.h"
#include "serial_driver.h"

//! How long a configuration change waits for the port tasks
const std::chrono::seconds DefaultPortTasksWaitTimeout(10);

//! config/Save is running or the service is stopped
class TSerialDriverCoreBusyError: public std::runtime_error
{
public:
    TSerialDriverCoreBusyError();
};

//! No polled config device matches the target, and for a task no port is given either
class TTaskTargetNotFoundError: public std::runtime_error
{
public:
    TTaskTargetNotFoundError();
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
        PollingRestartFailed
    };

    TApplyConfigError(TReason reason, const std::string& message);

    TReason GetReason() const;

private:
    TReason Reason;
};

/**
 * @brief Owns everything that accesses the ports: the driver polling the configuration
 *        and the executors of the ports outside it
 */
class TSerialDriverCore: public util::TNonCopyable
{
public:
    //! The task gets the config device it addresses, null if there is none, and the current parameters cache
    typedef std::function<PSerialClientTask(PSerialDevice deviceFromConfig, PDeviceParametersCache cache)> TMakeTaskFn;

    //! A config device by its id, otherwise a port and a config device on it by its address
    struct TTaskTarget
    {
        std::string DeviceId;

        //! Not set: only DeviceId is searched
        std::optional<TPortSettings> Port;

        std::string SlaveId;
        //! Empty matches a Modbus device
        std::string DeviceType;
    };

    /**
     * @param handlerConfig the configuration loaded at the start, null starts without polling.
     *        The service starts without polling too if the polling can not be created for it
     * @param configLoader ApplyConfig loads and writes the configuration with it
     * @param debugFromCommandLine debug logging stays enabled after ApplyConfig whatever "debug" is
     * @param portTasksWaitTimeout how long ApplyConfig waits for the port tasks
     *
     * @throws std::invalid_argument if mqttDriver is null
     */
    TSerialDriverCore(WBMQTT::PDeviceDriver mqttDriver,
                      PHandlerConfig handlerConfig,
                      TConfigLoader& configLoader,
                      bool debugFromCommandLine,
                      std::chrono::milliseconds portTasksWaitTimeout = DefaultPortTasksWaitTimeout);
    ~TSerialDriverCore();

    //! config/Save is running or the service is stopped
    bool IsBusy();

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
     * @brief Checks the configuration, stops the port clients, writes the configuration and restarts
     *        the polling with it. If the service is stopped meanwhile, the polling is not restarted
     *
     * @throws TApplyConfigError. The file, the polled configuration and the MQTT devices are kept
     *         unless the reason is PollingRestartFailed. After a successful check the queued port tasks
     *         are cancelled whatever the result
     */
    void ApplyConfig(const Json::Value& config);

private:
    struct TPolledDevice
    {
        PSerialClient SerialClient;
        PSerialDevice Device;
    };

    WBMQTT::PDeviceDriver MqttDriver;
    TConfigLoader& ConfigLoader;
    bool DebugFromCommandLine;
    std::chrono::milliseconds PortTasksWaitTimeout;

    std::mutex ApplyConfigMutex;
    bool Started;

    std::mutex Mutex;
    bool Applying;
    bool Stopped;
    PMQTTSerialDriver SerialDriver;
    PHandlerConfig Config;
    PDeviceParametersCache ParametersCache;
    std::vector<PSerialClientTaskExecutor> Executors;

    //! The mutex is held by the caller
    bool IsBusyLocked() const;
    void ThrowIfBusy() const;
    TPolledDevice FindPolledDeviceLocked(const TTaskTarget& target);
    void RemoveUnusedExecutors();

    void ResumeClients(const std::vector<PSerialClient>& serialClients,
                       const std::vector<PSerialClientTaskExecutor>& executors);
    void DoApplyConfig(const Json::Value& config);
    void SwitchDriver(PHandlerConfig handlerConfig);
};
