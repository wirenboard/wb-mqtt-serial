#pragma once
#include "rpc_device_handler.h"
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
                                PHandlerConfig handlerConfig,
                                TRPCDeviceParametersCache& parametersCache);

    const TSerialDeviceFactory& DeviceFactory;
    const std::string DeviceType;
    const Json::Value& DeviceTemplate;
    const PHandlerConfig HandlerConfig;

    TRPCDeviceParametersCache& ParametersCache;
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
                                                            PHandlerConfig handlerConfig,
                                                            TRPCDeviceParametersCache& parametersCache);

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
typedef std::vector<std::pair<std::string, PRegister>> TRPCRegisterList;

TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& parameters,
                                    const std::string& fwVersion);
void CheckParametersConditions(const Json::Value& templateParams, Json::Value& parameters);
Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val);
