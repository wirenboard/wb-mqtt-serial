#pragma once
#include "rpc_device_handler.h"

template<> bool inline WBMQTT::JSON::Is<uint8_t>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline uint8_t WBMQTT::JSON::As<uint8_t>(const Json::Value& value)
{
    return value.asUInt() & 0xFF;
}

class TRPCDeviceLoadConfigRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceLoadConfigRequest(const TDeviceProtocolParams& protocolParams,
                                PSerialDevice device,
                                PDeviceTemplate deviceTemplate,
                                bool deviceFromConfig,
                                TRPCDeviceParametersCache& parametersCache);

    TRPCDeviceParametersCache& ParametersCache;
    bool IsWBDevice = false;
    std::string Group;
};

typedef std::shared_ptr<TRPCDeviceLoadConfigRequest> PRPCDeviceLoadConfigRequest;

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TDeviceProtocolParams& protocolParams,
                                                            PSerialDevice device,
                                                            PDeviceTemplate deviceTemplate,
                                                            bool deviceFromConfig,
                                                            TRPCDeviceParametersCache& parametersCache,
                                                            WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                            WBMQTT::TMqttRpcServer::TErrorCallback onError);

class TRPCDeviceLoadConfigSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceLoadConfigSerialClientTask(PRPCDeviceLoadConfigRequest request);
    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceLoadConfigRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCDeviceLoadConfigSerialClientTask> PRPCDeviceLoadConfigSerialClientTask;

/**
 * @brief Creates JSON object containing template parameters of specified group
 *        and their condition parameters (recoursive)
 *
 * @param templateParams - device template parameters JSON array or object
 * @param group - parameters group name
 * @param paramsList - string list reference to store found parameters names
 *
 * @return TRPCRegisterList - named PRegister list
 */
Json::Value GetTemplateParamsGroup(const Json::Value& templateParams,
                                   const std::string& group,
                                   std::list<std::string>& paramsList);

void CheckParametersConditions(const Json::Value& templateParams, Json::Value& parameters);
Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val);
