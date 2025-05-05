#include "rpc_port_driver_list.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const size_t MAX_TASK_EXECUTORS = 10;

    std::string GetRequestPortDescription(const Json::Value& request)
    {
        std::string path;
        if (WBMQTT::JSON::Get(request, "path", path)) {
            return path;
        }
        int portNumber;
        if (WBMQTT::JSON::Get(request, "ip", path) && WBMQTT::JSON::Get(request, "port", portNumber)) {
            return path + ":" + std::to_string(portNumber);
        }
        throw TRPCException("Port is not defined", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

//==========================================================
//              TSerialClientTaskExecutor
//==========================================================

TSerialClientTaskExecutor::TSerialClientTaskExecutor(PPort port): Port(port)
{
    Thread = std::thread([this]() {
        TSerialClientDeviceAccessHandler lastAccessedDevice(nullptr);
        while (true) {
            std::unique_lock<std::mutex> lock(Mutex);
            TasksCv.wait(lock, [this]() { return !Tasks.empty() || !Running; });
            if (!Running) {
                break;
            }
            if (Tasks.empty()) {
                continue;
            }
            std::vector<PSerialClientTask> tasksToRun;
            Tasks.swap(tasksToRun);
            lock.unlock();
            for (auto& task: tasksToRun) {
                try {
                    task->Run(Port, lastAccessedDevice);
                } catch (const std::exception& e) {
                    LOG(Error) << "Error while running task: " << e.what();
                }
            }
            lock.lock();
            if (Tasks.empty()) {
                Port->Close();
            }
        }
    });
}

TSerialClientTaskExecutor::~TSerialClientTaskExecutor()
{
    Running = false;
    TasksCv.notify_all();
    Thread.join();
}

void TSerialClientTaskExecutor::AddTask(PSerialClientTask task)
{
    if (!Running) {
        return;
    }
    {
        std::unique_lock<std::mutex> lock(Mutex);
        Tasks.push_back(task);
    }
    TasksCv.notify_all();
}

PPort TSerialClientTaskExecutor::GetPort() const
{
    return Port;
}

bool TSerialClientTaskExecutor::IsIdle() const
{
    std::unique_lock<std::mutex> lock(Mutex);
    return Tasks.empty();
}

//==========================================================
//              TSerialClientTaskRunner
//==========================================================

TSerialClientTaskRunner::TSerialClientTaskRunner(PMQTTSerialDriver serialDriver): SerialDriver(serialDriver)
{}

void TSerialClientTaskRunner::RunTask(const Json::Value& request, PSerialClientTask task)
{
    auto portDescription = GetRequestPortDescription(request);

    auto portDrivers = SerialDriver->GetPortDrivers();
    auto portDriver =
        std::find_if(portDrivers.begin(), portDrivers.end(), [&portDescription](const PSerialPortDriver& driver) {
            return driver->GetSerialClient()->GetPort()->GetDescription() == portDescription;
        });
    if (portDriver != portDrivers.end()) {
        (*portDriver)->GetSerialClient()->AddTask(task);
        return;
    }

    std::unique_lock<std::mutex> lock(TaskExecutorsMutex);
    auto executor = std::find_if(TaskExecutors.begin(),
                                 TaskExecutors.end(),
                                 [&portDescription](PSerialClientTaskExecutor executor) {
                                     return executor->GetPort()->GetDescription() == portDescription;
                                 });
    if (executor == TaskExecutors.end()) {
        auto newExecutor = std::make_shared<TSerialClientTaskExecutor>(InitPort(request));
        TaskExecutors.push_back(newExecutor);
        newExecutor->AddTask(task);
        RemoveUnusedExecutors();
    } else {
        (*executor)->AddTask(task);
    }
}

void TSerialClientTaskRunner::RemoveUnusedExecutors()
{
    while (TaskExecutors.size() > MAX_TASK_EXECUTORS) {
        auto executorIt = std::find_if(TaskExecutors.begin(),
                                       TaskExecutors.end(),
                                       [](PSerialClientTaskExecutor executor) { return executor->IsIdle(); });
        if (executorIt == TaskExecutors.end()) {
            break;
        }
        TaskExecutors.erase(executorIt);
    }
}
