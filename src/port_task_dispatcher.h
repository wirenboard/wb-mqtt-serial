#pragma once

#include <mutex>
#include <optional>

#include "port/port_settings.h"

#include "device_parameters_cache.h"
#include "serial_client_task_executor.h"
#include "serial_driver.h"

//! No polled config device matches the target, and for a task no port is given either
class TTaskTargetNotFoundError: public std::runtime_error
{
public:
    TTaskTargetNotFoundError();
};

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

//! The task gets the config device it addresses, null if there is none, and the current parameters cache
typedef std::function<PSerialClientTask(PSerialDevice deviceFromConfig, PDeviceParametersCache cache)> TMakeTaskFn;

/**
 * @brief Owns the driver polling the configuration and the executors of the ports outside it,
 *        sends a task to the client polling the target or to the executor of the target port.
 *        Start, Switch, Pause and Resume are called one at a time
 */
class TPortTaskDispatcher: public util::TNonCopyable
{
public:
    //! The executors open the ports made by portFactory
    explicit TPortTaskDispatcher(TFeaturePortFactoryFn portFactory);
    ~TPortTaskDispatcher();

    //! Thread-safe
    //! @throws TTaskTargetNotFoundError, the exceptions of makeTask
    void AddTask(const TTaskTarget& target, const TMakeTaskFn& makeTask);

    //! Thread-safe. Suspends or resumes the polling of the config device of the target
    //! @throws TTaskTargetNotFoundError, std::runtime_error of the client
    void SetPoll(const TTaskTarget& target, bool poll);

    //! Starts the driver, the drivers set later are started by Switch
    void Start();

    //! Stops the previous driver and executors and sets the driver, it is started after Start
    void Switch(PMQTTSerialDriver driver, PDeviceParametersCache cache);

    /**
     * @brief Cancels the queued tasks and waits for the running ones on the clients of the started driver
     *        and the executors
     *
     * @returns false if some of them do not stop in time, then all of them are resumed
     */
    bool Pause(std::chrono::milliseconds timeout);

    void Resume();

private:
    struct TPolledDevice
    {
        PSerialClient SerialClient;
        PSerialDevice Device;
    };

    TFeaturePortFactoryFn PortFactory;

    std::mutex Mutex;
    PMQTTSerialDriver SerialDriver;
    PDeviceParametersCache ParametersCache;
    std::vector<PSerialClientTaskExecutor> Executors;

    bool Started = false;
    std::vector<PSerialClient> PausedClients;
    std::vector<PSerialClientTaskExecutor> PausedExecutors;

    //! The mutex is held by the caller
    TPolledDevice FindPolledDeviceLocked(const TTaskTarget& target);
    void RemoveUnusedExecutors();
};
