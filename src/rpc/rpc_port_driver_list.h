#pragma once

#include "rpc_exception.h"
#include "rpc_port_settings.h"
#include "serial_driver.h"

class TSerialClientTaskExecutor: public util::TNonCopyable
{
public:
    TSerialClientTaskExecutor(PFeaturePort port);
    ~TSerialClientTaskExecutor();

    void AddTask(PSerialClientTask task);

    PFeaturePort GetPort() const;

    bool IsIdle() const;

private:
    PFeaturePort Port;

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

bool PortMatches(const TRPCPortSettings& requestedPortSettings, const TFeaturePort& port);

class ITaskRunner
{
public:
    virtual ~ITaskRunner() = default;

    //! Run a task on the port, whether the driver polls it or not
    virtual void RunTask(const TRPCPortSettings& portSettings, PSerialClientTask task) = 0;
};

class TSerialClientTaskRunner: public ITaskRunner
{
public:
    TSerialClientTaskRunner(PMQTTSerialDriver serialDriver);

    TSerialClientParams GetSerialClientParams(const Json::Value& request);
    void RunTask(const Json::Value& request, PSerialClientTask task);
    void RunTask(const TRPCPortSettings& portSettings, PSerialClientTask task) override;

private:
    PMQTTSerialDriver SerialDriver;

    std::vector<PSerialClientTaskExecutor> TaskExecutors;
    std::mutex TaskExecutorsMutex;

    PSerialClient FindSerialClient(const TRPCPortSettings& portSettings);
    void RunTaskOnOwnPort(const TRPCPortSettings& portSettings, PSerialClientTask task);
    void RemoveUnusedExecutors();
};
