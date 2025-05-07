#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_load_request.h"
#include "serial_client.h"

class TRPCPortLoadRawRequest: public TRPCPortLoadRequest
{
public:
    size_t ResponseSize;
};

typedef std::shared_ptr<TRPCPortLoadRawRequest> PRPCPortLoadRawRequest;

class TRPCPortLoadRawSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortLoadRawSerialClientTask(const Json::Value& request,
                                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                    WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCPortLoadRawRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortLoadRawSerialClientTask> PRPCPortLoadRawSerialClientTask;
