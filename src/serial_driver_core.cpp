#include "serial_driver_core.h"
#include "log.h"
#include "port/serial_port.h"
#include "port/tcp_port.h"
#include <exception>
#include <wblib/utils.h>

#define LOG(logger) ::logger.Log() << "[serial] "

namespace
{
    const size_t MAX_TASK_EXECUTORS = 5;

    PFeaturePort InitPort(const TPortSettings& portSettings)
    {
        if (const auto* tcpPort = std::get_if<TFramedTcpPortSettings>(&portSettings)) {
            LOG(Debug) << "Create tcp port: " << tcpPort->GetDescription();
            return std::make_shared<TFeaturePort>(std::make_shared<TTcpPort>(*tcpPort), tcpPort->ModbusTcp);
        }
        const auto& serialPort = std::get<TSerialPortSettings>(portSettings);
        LOG(Debug) << "Create serial port: " << serialPort.Device;
        return std::make_shared<TFeaturePort>(std::make_shared<TSerialPort>(serialPort), false);
    }

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
}

TSerialDriverCoreBusyError::TSerialDriverCoreBusyError()
    : std::runtime_error("config/Save is running or the service is stopped")
{}

TTaskTargetNotFoundError::TTaskTargetNotFoundError(): std::runtime_error("no polled device with the id and no port")
{}

TApplyConfigError::TApplyConfigError(TReason reason, const std::string& message)
    : std::runtime_error(message),
      Reason(reason)
{}

TApplyConfigError::TReason TApplyConfigError::GetReason() const
{
    return Reason;
}

TSerialDriverCore::TSerialDriverCore(WBMQTT::PDeviceDriver mqttDriver,
                                     PHandlerConfig handlerConfig,
                                     TConfigLoader& configLoader,
                                     bool debugFromCommandLine,
                                     std::chrono::milliseconds portTasksWaitTimeout)
    : MqttDriver(mqttDriver),
      ConfigLoader(configLoader),
      DebugFromCommandLine(debugFromCommandLine),
      PortTasksWaitTimeout(portTasksWaitTimeout),
      Started(false),
      Applying(false),
      Stopped(false)
{
    if (!MqttDriver) {
        throw std::invalid_argument("no MQTT driver to publish the devices");
    }
    if (handlerConfig) {
        // The service goes on without the polling, config/Save can fix the configuration
        try {
            SerialDriver = std::make_shared<TMQTTSerialDriver>(MqttDriver, handlerConfig);
            Config = handlerConfig;
        } catch (const std::exception& e) {
            LOG(Error) << "the polling is not started: " << e.what();
        }
    }
    ParametersCache = MakeParametersCache(Config);
}

TSerialDriverCore::~TSerialDriverCore()
{
    SerialDriver.reset();
    Executors.clear();
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

TSerialDriverCore::TPolledDevice TSerialDriverCore::FindPolledDeviceLocked(const TTaskTarget& target)
{
    if (!SerialDriver) {
        return {};
    }
    if (!target.DeviceId.empty()) {
        for (const auto& portDriver: SerialDriver->GetPortDrivers()) {
            auto serialClient = portDriver->GetSerialClient();
            for (const auto& device: serialClient->GetDevices()) {
                if (device->DeviceConfig()->Id == target.DeviceId) {
                    return {serialClient, device};
                }
            }
        }
    }
    if (!target.Port) {
        return {};
    }
    for (const auto& portDriver: SerialDriver->GetPortDrivers()) {
        auto serialClient = portDriver->GetSerialClient();
        if (!PortMatches(*target.Port, *serialClient->GetPort())) {
            continue;
        }
        for (const auto& device: serialClient->GetDevices()) {
            if (device->DeviceConfig()->SlaveId == target.SlaveId &&
                (device->DeviceConfig()->DeviceType == target.DeviceType ||
                 (target.DeviceType.empty() && WBMQTT::StringStartsWith(device->Protocol()->GetName(), "modbus"))))
            {
                return {serialClient, device};
            }
        }
        return {serialClient, nullptr};
    }
    return {};
}

void TSerialDriverCore::SetPoll(const TTaskTarget& target, bool poll)
{
    // SuspendPoll calls the callbacks of the port driver, ApplyConfig does not destroy it while the mutex is held
    std::lock_guard<std::mutex> lock(Mutex);
    ThrowIfBusy();
    auto polledDevice = FindPolledDeviceLocked(target);
    if (!polledDevice.Device) {
        throw TTaskTargetNotFoundError();
    }
    if (poll) {
        polledDevice.SerialClient->ResumePoll(polledDevice.Device);
    } else {
        polledDevice.SerialClient->SuspendPoll(polledDevice.Device, std::chrono::steady_clock::now());
    }
}

void TSerialDriverCore::AddTask(const TTaskTarget& target, const TMakeTaskFn& makeTask)
{
    std::lock_guard<std::mutex> lock(Mutex);
    ThrowIfBusy();
    auto polledDevice = FindPolledDeviceLocked(target);
    if (polledDevice.SerialClient) {
        polledDevice.SerialClient->AddTask(makeTask(polledDevice.Device, ParametersCache));
        return;
    }
    if (!target.Port) {
        throw TTaskTargetNotFoundError();
    }
    auto executor = std::find_if(Executors.begin(), Executors.end(), [&target](const auto& executor) {
        return PortMatches(*target.Port, *executor->GetPort());
    });
    if (executor != Executors.end()) {
        (*executor)->AddTask(makeTask(nullptr, ParametersCache));
        return;
    }
    auto task = makeTask(nullptr, ParametersCache);
    RemoveUnusedExecutors();
    auto newExecutor = std::make_shared<TSerialClientTaskExecutor>(InitPort(*target.Port));
    Executors.push_back(newExecutor);
    newExecutor->AddTask(task);
}

void TSerialDriverCore::RemoveUnusedExecutors()
{
    while (Executors.size() >= MAX_TASK_EXECUTORS) {
        auto executorIt =
            std::find_if(Executors.begin(), Executors.end(), [](const auto& executor) { return executor->IsIdle(); });
        if (executorIt == Executors.end()) {
            break;
        }
        Executors.erase(executorIt);
    }
}

PHandlerConfig TSerialDriverCore::GetConfig()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Config;
}

void TSerialDriverCore::Start()
{
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    Started = true;
    PMQTTSerialDriver serialDriver;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        serialDriver = SerialDriver;
    }
    if (serialDriver) {
        serialDriver->Start();
    }
}

void TSerialDriverCore::Stop()
{
    {
        // ApplyConfig, if it runs, does not start a new polling then
        std::lock_guard<std::mutex> lock(Mutex);
        Stopped = true;
    }
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    PMQTTSerialDriver serialDriver;
    std::vector<PSerialClientTaskExecutor> executors;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        serialDriver = SerialDriver;
        executors.swap(Executors);
    }
    if (serialDriver && Started) {
        serialDriver->Stop();
    }
    Started = false;
    for (const auto& executor: executors) {
        executor->RequestStop();
    }
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

void TSerialDriverCore::ResumeClients(const std::vector<PSerialClient>& serialClients,
                                      const std::vector<PSerialClientTaskExecutor>& executors)
{
    for (const auto& serialClient: serialClients) {
        serialClient->Resume();
    }
    for (const auto& executor: executors) {
        executor->Resume();
    }
}

void TSerialDriverCore::DoApplyConfig(const Json::Value& config)
{
    std::lock_guard<std::mutex> applyConfigLock(ApplyConfigMutex);
    std::vector<PSerialClient> serialClients;
    std::vector<PSerialClientTaskExecutor> executors;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        // The clients of a driver which is not started are not run, there is nothing to stop
        if (SerialDriver && Started) {
            for (const auto& portDriver: SerialDriver->GetPortDrivers()) {
                serialClients.push_back(portDriver->GetSerialClient());
            }
        }
        executors = Executors;
    }
    PHandlerConfig handlerConfig;
    try {
        handlerConfig = ConfigLoader.Load(config);
    } catch (const std::exception& e) {
        throw TApplyConfigError(TApplyConfigError::TReason::ConfigInvalid, e.what());
    }
    for (const auto& serialClient: serialClients) {
        serialClient->RequestStop();
    }
    for (const auto& executor: executors) {
        executor->RequestStop();
    }
    auto deadline = std::chrono::steady_clock::now() + PortTasksWaitTimeout;
    size_t notStopped = 0;
    for (const auto& serialClient: serialClients) {
        if (!serialClient->WaitStopped(deadline)) {
            ++notStopped;
        }
    }
    for (const auto& executor: executors) {
        if (!executor->WaitStopped(deadline)) {
            ++notStopped;
        }
    }
    if (notStopped != 0) {
        ResumeClients(serialClients, executors);
        throw TApplyConfigError(TApplyConfigError::TReason::PortTasksTimeout,
                                std::to_string(notStopped) + " port client(s) did not stop in " +
                                    std::to_string(PortTasksWaitTimeout.count()) + " ms");
    }
    try {
        ConfigLoader.Write(config);
    } catch (const std::exception& e) {
        ResumeClients(serialClients, executors);
        throw TApplyConfigError(TApplyConfigError::TReason::WriteFailed,
                                std::string("unable to write the configuration: ") + e.what());
    }
    ::Debug.SetEnabled(DebugFromCommandLine || handlerConfig->Debug);
    serialClients.clear();
    executors.clear();
    try {
        SwitchDriver(handlerConfig);
    } catch (const std::exception& e) {
        throw TApplyConfigError(TApplyConfigError::TReason::PollingRestartFailed,
                                std::string("the configuration is written, but the polling is not started: ") +
                                    e.what());
    }
}

void TSerialDriverCore::SwitchDriver(PHandlerConfig handlerConfig)
{
    PMQTTSerialDriver oldDriver;
    std::vector<PSerialClientTaskExecutor> oldExecutors;
    {
        // The old configuration is reported until the new one is polled
        std::lock_guard<std::mutex> lock(Mutex);
        oldDriver.swap(SerialDriver);
        oldExecutors.swap(Executors);
    }
    if (oldDriver) {
        if (Started) {
            oldDriver->Stop();
        } else {
            oldDriver->ClearDevices();
        }
        oldDriver.reset();
    }
    oldExecutors.clear();

    auto parametersCache = MakeParametersCache(handlerConfig);
    PMQTTSerialDriver newDriver;
    std::exception_ptr error;
    try {
        newDriver = std::make_shared<TMQTTSerialDriver>(MqttDriver, handlerConfig);
    } catch (...) {
        error = std::current_exception();
    }
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (newDriver && !Stopped) {
            SerialDriver = newDriver;
            Config = handlerConfig;
            ParametersCache = parametersCache;
            // Under the mutex, so Stop does not miss the started driver
            if (Started) {
                newDriver->Start();
            }
            return;
        }
        Config = nullptr;
        ParametersCache = MakeParametersCache(nullptr);
    }
    if (error) {
        std::rethrow_exception(error);
    }
    newDriver->ClearDevices();
}
