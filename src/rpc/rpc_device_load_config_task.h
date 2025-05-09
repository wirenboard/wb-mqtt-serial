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

class TRPCDeviceLoadConfigRequest
{
public:
    TRPCDeviceLoadConfigRequest(const TSerialDeviceFactory& deviceFactory,
                                PDeviceTemplate deviceTemplate,
                                PHandlerConfig handlerConfig);

    const TSerialDeviceFactory& DeviceFactory;
    const std::string DeviceType;
    const Json::Value& DeviceTemplate;
    const PHandlerConfig HandlerConfig;

    bool ContinuousReadSupported = false;
    bool IsWBDevice = false;

    TSerialPortConnectionSettings SerialPortSettings;
    std::chrono::milliseconds ResponseTimeout = DefaultResponseTimeout;
    std::chrono::milliseconds FrameTimeout = DefaultFrameTimeout;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;
    uint8_t SlaveId = 0;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;

    PSerialDevice FindDevice(PPort port);
};

typedef std::shared_ptr<TRPCDeviceLoadConfigRequest> PRPCDeviceLoadConfigRequest;

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TSerialDeviceFactory& deviceFactory,
                                                            PDeviceTemplate deviceTemplate,
                                                            PHandlerConfig handlerConfig);

void ExecRPCDeviceLoadConfigRequest(PPort port, PRPCDeviceLoadConfigRequest rpcRequest);

class TRPCDeviceLoadConfigSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceLoadConfigSerialClientTask(PRPCDeviceLoadConfigRequest request);
    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCDeviceLoadConfigRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCDeviceLoadConfigSerialClientTask> PRPCDeviceLoadConfigSerialClientTask;

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val);
