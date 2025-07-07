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
};

class TSerialClientTaskRunner
{
public:
    TSerialClientTaskRunner(PMQTTSerialDriver serialDriver);

    TSerialClientParams GetSerialClientParams(const Json::Value& request);
    PSerialClientTaskExecutor GetTaskExecutor(const Json::Value& request);

    void RunTask(const Json::Value& request, PSerialClientTask task);

private:
    PMQTTSerialDriver SerialDriver;

    std::vector<PSerialClientTaskExecutor> TaskExecutors;
    std::mutex TaskExecutorsMutex;

    void RemoveUnusedExecutors();
};
