#pragma once

#include "rpc_config.h"
#include "rpc_exception.h"
#include "serial_driver.h"

class TSerialClientTaskExecutor: public util::TNonCopyable
{
public:
    TSerialClientTaskExecutor(PPort port);
    ~TSerialClientTaskExecutor();

    void AddTask(PSerialClientTask task);

    PPort GetPort() const;

    bool IsIdle() const;

private:
    PPort Port;

    mutable std::mutex Mutex;
    std::condition_variable TasksCv;
    std::vector<PSerialClientTask> Tasks;
    std::thread Thread;
    std::atomic<bool> Running;
    bool Idle;
};

typedef std::shared_ptr<TSerialClientTaskExecutor> PSerialClientTaskExecutor;

struct TSerialClientParams
{
    PSerialClient SerialClient;
    PSerialDevice Device;
    std::chrono::milliseconds PortResponseTimeout = RESPONSE_TIMEOUT_NOT_SET;
};

class TSerialClientTaskRunner
{
public:
    TSerialClientTaskRunner(PMQTTSerialDriver serialDriver);

    TSerialClientParams GetSerialClientParams(const Json::Value& request);

    /**
     * @brief Executes a serial client task based on the provided JSON request.
     *
     * This function searches for a serial client that matches the request's port.
     * If a matching client is found, the task is added to that client's task queue.
     * If no matching client is found, the task is executed on a dedicated executor.
     *
     * @param request The JSON value containing the parameters for the task.
     * @param task A pointer to the serial client task to be executed.
     */
    void RunTask(const Json::Value& request, PSerialClientTask task);

    /**
     * @brief Executes a given serial client task on the executor using the provided JSON request.
     *
     * @param request A JSON object containing parameters or data required for the task execution.
     * @param task The serial client task to be executed.
     */
    void RunTaskOnExecutor(const Json::Value& request, PSerialClientTask task);

private:
    PMQTTSerialDriver SerialDriver;

    std::vector<PSerialClientTaskExecutor> TaskExecutors;
    std::mutex TaskExecutorsMutex;

    void RemoveUnusedExecutors();
};
