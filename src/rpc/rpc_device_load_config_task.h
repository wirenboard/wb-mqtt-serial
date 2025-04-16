#pragma once
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
    TRPCDeviveLoadConfigRequest(const Json::Value& parameters, const TSerialDeviceFactory& deviceFactory);

    const Json::Value Parameters;
    const TSerialDeviceFactory& DeviceFactory;

    TSerialPortConnectionSettings SerialPortSettings;
    std::chrono::milliseconds ResponseTimeout;
    std::chrono::milliseconds FrameTimeout;
    std::chrono::milliseconds TotalTimeout;
    uint8_t SlaveId;
    std::string DeviceType;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCDeviveLoadConfigRequest> PRPCDeviveLoadConfigRequest;

PRPCDeviveLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const Json::Value& parameters,
                                                            const TSerialDeviceFactory& deviceFactory);

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
