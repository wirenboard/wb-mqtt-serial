#include "port_task_dispatcher.h"
#include "log.h"
#include <wblib/utils.h>

#define LOG(logger) ::logger.Log() << "[serial] "

namespace
{
    const size_t MAX_TASK_EXECUTORS = 5;
}

TTaskTargetNotFoundError::TTaskTargetNotFoundError(): std::runtime_error("no polled device with the id and no port")
{}

TPortTaskDispatcher::TPortTaskDispatcher(TFeaturePortFactoryFn portFactory): PortFactory(portFactory)
{}

TPortTaskDispatcher::~TPortTaskDispatcher()
{
    SerialDriver.reset();
    Executors.clear();
}

TPortTaskDispatcher::TPolledDevice TPortTaskDispatcher::FindPolledDeviceLocked(const TTaskTarget& target)
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

void TPortTaskDispatcher::SetPoll(const TTaskTarget& target, bool poll)
{
    // SuspendPoll calls the callbacks of the port driver, Switch does not destroy it while the mutex is held
    std::lock_guard<std::mutex> lock(Mutex);
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

void TPortTaskDispatcher::AddTask(const TTaskTarget& target, const TMakeTaskFn& makeTask)
{
    std::lock_guard<std::mutex> lock(Mutex);
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
    auto port = PortFactory(TFeaturePortSettings{*target.Port});
    LOG(Debug) << "Create port: " << port->GetDescription();
    auto newExecutor = std::make_shared<TSerialClientTaskExecutor>(port);
    Executors.push_back(newExecutor);
    newExecutor->AddTask(task);
}

void TPortTaskDispatcher::RemoveUnusedExecutors()
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

void TPortTaskDispatcher::Start()
{
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

void TPortTaskDispatcher::Switch(PMQTTSerialDriver driver, PDeviceParametersCache cache)
{
    PMQTTSerialDriver oldDriver;
    std::vector<PSerialClientTaskExecutor> oldExecutors;
    {
        std::lock_guard<std::mutex> lock(Mutex);
        oldDriver.swap(SerialDriver);
        oldExecutors.swap(Executors);
        SerialDriver = driver;
        ParametersCache = cache;
    }
    PausedClients.clear();
    PausedExecutors.clear();
    if (oldDriver) {
        if (Started) {
            oldDriver->Stop();
        } else {
            oldDriver->ClearDevices();
        }
        oldDriver.reset();
    }
    for (const auto& executor: oldExecutors) {
        executor->RequestStop();
    }
    oldExecutors.clear();
    if (driver && Started) {
        driver->Start();
    }
}

bool TPortTaskDispatcher::Pause(std::chrono::milliseconds timeout)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        // The clients of a driver which is not started are not run, there is nothing to stop
        if (SerialDriver && Started) {
            for (const auto& portDriver: SerialDriver->GetPortDrivers()) {
                PausedClients.push_back(portDriver->GetSerialClient());
            }
        }
        PausedExecutors = Executors;
    }
    for (const auto& serialClient: PausedClients) {
        serialClient->RequestStop();
    }
    for (const auto& executor: PausedExecutors) {
        executor->RequestStop();
    }
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool stopped = true;
    for (const auto& serialClient: PausedClients) {
        stopped = serialClient->WaitStopped(deadline) && stopped;
    }
    for (const auto& executor: PausedExecutors) {
        stopped = executor->WaitStopped(deadline) && stopped;
    }
    if (!stopped) {
        Resume();
    }
    return stopped;
}

void TPortTaskDispatcher::Resume()
{
    for (const auto& serialClient: PausedClients) {
        serialClient->Resume();
    }
    for (const auto& executor: PausedExecutors) {
        executor->Resume();
    }
    PausedClients.clear();
    PausedExecutors.clear();
}
