#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_load_request.h"
#include "serial_client.h"

class TRPCPortLoadModbusRequest: public TRPCPortLoadRequest
{
public:
    TRPCPortLoadModbusRequest(TRPCDeviceParametersCache& parametersCache);
    TRPCDeviceParametersCache& ParametersCache;
    uint8_t SlaveId;
    uint16_t Address;
    size_t Count;
    Modbus::EFunction Function;
};

typedef std::shared_ptr<TRPCPortLoadModbusRequest> PRPCPortLoadModbusRequest;

class TRPCPortLoadModbusSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortLoadModbusSerialClientTask(const Json::Value& request,
                                       WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                       WBMQTT::TMqttRpcServer::TErrorCallback onError,
                                       TRPCDeviceParametersCache& parametersCache);

    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCPortLoadModbusRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortLoadModbusSerialClientTask> PRPCPortLoadModbusSerialClientTask;
