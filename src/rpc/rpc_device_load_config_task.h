#pragma once
#include "rpc_port_handler.h"
#include "serial_client.h"
#include <chrono>
#include <wblib/rpc.h>

template<> bool inline WBMQTT::JSON::Is<uint8_t>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline uint8_t WBMQTT::JSON::As<uint8_t>(const Json::Value& value)
{
    return value.asUInt() & 0xFF;
}

class TRPCDeviveLoadConfigRequest
{
public:
    TSerialPortConnectionSettings SerialPortSettings;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;
    uint8_t SlaveId;
    std::string DeviceType;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCDeviveLoadConfigRequest> PRPCDeviveLoadConfigRequest;

PRPCDeviveLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request);

void ExecRPCDeviveLoadConfigRequest(TPort& port, PRPCDeviveLoadConfigRequest rpcRequest);

class TRPCDeviceLoadConfigSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceLoadConfigSerialClientTask(PRPCDeviveLoadConfigRequest request);
    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCDeviveLoadConfigRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCDeviceLoadConfigSerialClientTask> PRPCDeviceLoadConfigSerialClientTask;
