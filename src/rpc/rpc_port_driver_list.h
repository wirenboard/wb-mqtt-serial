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
};

class TSerialClientTaskRunner: public ITaskRunner
{
public:
    TSerialClientTaskRunner(TSerialDriverCore& serialDriverCore);

    //! Suspends or resumes the polling of the config device of the request
    //! @throws TRPCException with "config-busy" during config/Save and after the service stop
    void SetPoll(const Json::Value& request, bool poll);

    //! Adds the task to the client of the config device or the port of the request
    //! @throws TRPCException with "config-busy" during config/Save and after the service stop
    void RunTask(const Json::Value& request, const TSerialDriverCore::TMakeTaskFn& makeTask);

    void RunTask(const TPortSettings& portSettings, PSerialClientTask task) override;

private:
    TSerialDriverCore& SerialDriverCore;
};
