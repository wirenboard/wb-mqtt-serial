#pragma once

#include "rpc_port_setup_request.h"
#include "serial_client.h"
#include <chrono>
#include <wblib/rpc.h>

class TRPCPortSetupSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortSetupSerialClientTask(PRPCPortSetupRequest request);

    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCPortSetupRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortSetupSerialClientTask> PRPCPortSetupSerialClientTask;
