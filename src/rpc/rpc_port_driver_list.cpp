#include "rpc_port_driver_list.h"
#include "port/serial_port.h"
#include "port/tcp_port.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const size_t MAX_TASK_EXECUTORS = 5;

    TRPCPortSettings ParseRequestPort(const Json::Value& request)
    {
        return ParseRPCPort(request, request.get("protocol", "modbus").asString() == "modbus-tcp");
    }

    PFeaturePort InitPort(const TRPCPortSettings& portSettings)
    {
        if (const auto* tcpPort = std::get_if<TRPCTcpPortSettings>(&portSettings)) {
            LOG(Debug) << "Create tcp port: " << tcpPort->GetDescription();
            return std::make_shared<TFeaturePort>(std::make_shared<TTcpPort>(*tcpPort), tcpPort->ModbusTcp);
        }
        const auto& serialPort = std::get<TSerialPortSettings>(portSettings);
        LOG(Debug) << "Create serial port: " << serialPort.Device;
        return std::make_shared<TFeaturePort>(std::make_shared<TSerialPort>(serialPort), false);
    }
}

bool PortMatches(const TRPCPortSettings& requestedPortSettings, const TFeaturePort& port)
{
    const auto& basePort = *port.GetBasePort();
    if (const auto* serialPort = dynamic_cast<const TSerialPort*>(&basePort)) {
        const auto* requested = std::get_if<TSerialPortSettings>(&requestedPortSettings);
        return requested != nullptr && serialPort->GetInitialSettings().Device == requested->Device;
    }
    if (const auto* tcpPort = dynamic_cast<const TTcpPort*>(&basePort)) {
        const auto* requested = std::get_if<TRPCTcpPortSettings>(&requestedPortSettings);
        const auto& settings = tcpPort->GetInitialSettings();
        return requested != nullptr && settings.Address == requested->Address && settings.Port == requested->Port;
    }
    return false;
}

//==========================================================
//              TSerialClientTaskExecutor
//==========================================================

TSerialClientTaskExecutor::TSerialClientTaskExecutor(PFeaturePort port): Port(port), Running(true), Idle(true)
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
            Idle = false;
            std::vector<PSerialClientTask> tasksToRun;
            Tasks.swap(tasksToRun);
            lock.unlock();
            for (auto& task: tasksToRun) {
                try {
                    task->Run(Port, lastAccessedDevice, std::list<PSerialDevice>());
                } catch (const std::exception& e) {
                    LOG(Error) << "Error while running task: " << e.what();
                }
            }
            lock.lock();
            if (Tasks.empty()) {
                Idle = true;
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

PFeaturePort TSerialClientTaskExecutor::GetPort() const
{
    return Port;
}

bool TSerialClientTaskExecutor::IsIdle() const
{
    std::unique_lock<std::mutex> lock(Mutex);
    return Idle;
}

//==========================================================
//              TSerialClientTaskRunner
//==========================================================

TSerialClientTaskRunner::TSerialClientTaskRunner(PMQTTSerialDriver serialDriver): SerialDriver(serialDriver)
{}

TSerialClientParams TSerialClientTaskRunner::GetSerialClientParams(const Json::Value& request)
{
    TSerialClientParams params;

    auto deviceId = request["device_id"];
    if (SerialDriver && deviceId.isString()) {
        auto id = deviceId.asString();
        for (auto driver: SerialDriver->GetPortDrivers()) {
            for (auto device: driver->GetSerialClient()->GetDevices()) {
                if (device->DeviceConfig()->Id == id) {
                    params.SerialClient = driver->GetSerialClient();
                    params.Device = device;
                    return params;
                }
            }
        }
    }

    params.SerialClient = FindSerialClient(ParseRequestPort(request));
    if (params.SerialClient) {
        auto slaveId = request["slave_id"].asString();
        auto deviceType = request["device_type"].asString();
        for (auto device: params.SerialClient->GetDevices()) {
            if (device->DeviceConfig()->SlaveId == slaveId &&
                (device->DeviceConfig()->DeviceType == deviceType ||
                 (deviceType.empty() && WBMQTT::StringStartsWith(device->Protocol()->GetName(), "modbus"))))
            {
                params.Device = device;
                break;
            }
        }
    }

    return params;
}

PSerialClient TSerialClientTaskRunner::FindSerialClient(const TRPCPortSettings& portSettings)
{
    if (!SerialDriver) {
        return nullptr;
    }
    auto portDrivers = SerialDriver->GetPortDrivers();
    auto portDriver =
        std::find_if(portDrivers.begin(), portDrivers.end(), [&portSettings](const PSerialPortDriver& driver) {
            return PortMatches(portSettings, *driver->GetSerialClient()->GetPort());
        });
    return portDriver == portDrivers.end() ? nullptr : (*portDriver)->GetSerialClient();
}

void TSerialClientTaskRunner::RunTask(const Json::Value& request, PSerialClientTask task)
{
    auto params = GetSerialClientParams(request);
    if (params.SerialClient) {
        params.SerialClient->AddTask(task);
        return;
    }
    RunTaskOnOwnPort(ParseRequestPort(request), task);
}

void TSerialClientTaskRunner::RunTask(const TRPCPortSettings& portSettings, PSerialClientTask task)
{
    auto serialClient = FindSerialClient(portSettings);
    if (serialClient) {
        serialClient->AddTask(task);
        return;
    }
    RunTaskOnOwnPort(portSettings, task);
}

void TSerialClientTaskRunner::RunTaskOnOwnPort(const TRPCPortSettings& portSettings, PSerialClientTask task)
{
    std::unique_lock<std::mutex> lock(TaskExecutorsMutex);
    auto executor =
        std::find_if(TaskExecutors.begin(), TaskExecutors.end(), [&portSettings](PSerialClientTaskExecutor executor) {
            return PortMatches(portSettings, *executor->GetPort());
        });
    if (executor != TaskExecutors.end()) {
        (*executor)->AddTask(task);
        return;
    }
    RemoveUnusedExecutors();
    auto newExecutor = std::make_shared<TSerialClientTaskExecutor>(InitPort(portSettings));
    TaskExecutors.push_back(newExecutor);
    newExecutor->AddTask(task);
}

void TSerialClientTaskRunner::RemoveUnusedExecutors()
{
    while (TaskExecutors.size() >= MAX_TASK_EXECUTORS) {
        auto executorIt = std::find_if(TaskExecutors.begin(),
                                       TaskExecutors.end(),
                                       [](PSerialClientTaskExecutor executor) { return executor->IsIdle(); });
        if (executorIt == TaskExecutors.end()) {
            break;
        }
        TaskExecutors.erase(executorIt);
    }
}
