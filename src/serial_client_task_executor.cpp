#include "serial_client_task_executor.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial] "

TSerialClientTaskExecutor::TSerialClientTaskExecutor(PFeaturePort port)
    : Port(port),
      StopRequested(false),
      Running(true),
      Idle(true)
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
            // One by one, so RequestStop cancels every task which has not started
            while (!Tasks.empty()) {
                auto task = Tasks.front();
                Tasks.pop_front();
                lock.unlock();
                try {
                    task->Run(Port, lastAccessedDevice, std::list<PSerialDevice>());
                } catch (const std::exception& e) {
                    LOG(Error) << "Error while running task: " << e.what();
                }
                lock.lock();
            }
            Idle = true;
            Port->Close();
            IdleCv.notify_all();
        }
    });
}

TSerialClientTaskExecutor::~TSerialClientTaskExecutor()
{
    {
        std::unique_lock<std::mutex> lock(Mutex);
        Running = false;
    }
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
        if (StopRequested) {
            task->Cancel();
        }
        Tasks.push_back(task);
    }
    TasksCv.notify_all();
}

void TSerialClientTaskExecutor::RequestStop()
{
    std::unique_lock<std::mutex> lock(Mutex);
    StopRequested = true;
    for (const auto& task: Tasks) {
        task->Cancel();
    }
}

bool TSerialClientTaskExecutor::WaitStopped(std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock(Mutex);
    return IdleCv.wait_until(lock, deadline, [this]() { return Idle; });
}

void TSerialClientTaskExecutor::Resume()
{
    std::unique_lock<std::mutex> lock(Mutex);
    StopRequested = false;
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
