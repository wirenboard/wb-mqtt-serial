#pragma once

#include "rpc_exception.h"
#include "rpc_port_settings.h"
#include "serial_driver_core.h"

class ITaskRunner
{
public:
    virtual ~ITaskRunner() = default;

    //! Run a task on the port, whether the driver polls it or not
    virtual void RunTask(const TPortSettings& portSettings, PSerialClientTask task) = 0;

    //! Adds the task to the client of the config device or the port of the request
    //! @throws TRPCException with "config-busy" during a configuration or template change and after the service stop
    virtual void RunTask(const Json::Value& request, const TMakeTaskFn& makeTask) = 0;
};

class TSerialClientTaskRunner: public ITaskRunner
{
public:
    TSerialClientTaskRunner(TSerialDriverCore& serialDriverCore);

    //! Suspends or resumes the polling of the config device of the request
    //! @throws TRPCException with "config-busy" during a configuration or template change and after the service stop
    void SetPoll(const Json::Value& request, bool poll);

    void RunTask(const Json::Value& request, const TMakeTaskFn& makeTask) override;
    void RunTask(const TPortSettings& portSettings, PSerialClientTask task) override;

private:
    TSerialDriverCore& SerialDriverCore;
};
