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
    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceLoadConfigRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCDeviceLoadConfigSerialClientTask> PRPCDeviceLoadConfigSerialClientTask;
typedef std::vector<std::pair<std::string, PRegister>> TRPCRegisterList;

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

/**
 * @brief Creates named PRegister list based on template parameters JSON array or object.
 *
 * @param protocolParams - device protocol params for LoadRegisterConfig call
 * @param device - serial device object pointer for TRegister object creation
 * @param templateParams - device template parameters JSON array or object
 * @param parameters - known parameters JSON object, where the key is the parameter id and the value is the known
 *                     parameter value, for example: {"baudrate": 96, "in1_mode": 2}
 *                     used to exclule known parameters from regiter list
 * @param fwVersion - device firmvare version string, used to exclude parameters unsupporterd by firmware
 *
 * @return TRPCRegisterList - named PRegister list
 */
TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& templateParams,
                                    const Json::Value& parameters,
                                    const std::string& fwVersion);

void CheckParametersConditions(const Json::Value& templateParams, Json::Value& parameters);
Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val);
