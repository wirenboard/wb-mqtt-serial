#pragma once

#include "rpc_port_load_request.h"
#include "serial_client.h"
#include <chrono>
#include <wblib/rpc.h>

class TRPCPortLoadSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortLoadSerialClientTask(PRPCPortLoadRequest request);

    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCPortLoadRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortLoadSerialClientTask> PRPCPortLoadSerialClientTask;
