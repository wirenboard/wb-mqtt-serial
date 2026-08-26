#pragma once
#include "rpc_device_handler.h"

class TRPCDeviceSetRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                         PSerialDevice device,
                         PDeviceTemplate deviceTemplate,
                         bool deviceFromConfig);

    Json::Value Channels;
    Json::Value Parameters;

    /**
     * @brief Template declarations of the requested channels with every condition variant,
     *        "id" is set to the channel name.
     *
     * @throws TRPCException if a channel is read only or not found in the template
     */
    Json::Value GetChannelDeclarations();

    /**
     * @brief Template declarations of the requested parameters with every condition variant, "id" is set.
     *        Read only declarations and parameters not found in the template are skipped, they are not written.
     */
    Json::Value GetParameterDeclarations();

    //! Registers to read: parameters referenced by the conditions of the declarations and absent from the request
    TRPCRegisterList GetConditionParametersRegisterList(const Json::Value& channels, const Json::Value& parameters);

    /**
     * @brief Keeps the declaration with a true condition of every channel or parameter. Conditions over
     *        parameters missing from conditionValues are evaluated with an undefined value.
     *        An item without a true condition is skipped and is not an error.
     *
     * @param kind - "Channel" or "Parameter" for the error message
     * @throws TRPCException if several declarations of an item match at once
     */
    Json::Value SelectActingDeclarations(const std::string& kind,
                                         const Json::Value& declarations,
                                         const Json::Value& conditionValues);

    //! Setup items of the acting declarations with the request values
    TDeviceSetupItems CreateSetupItems(const Json::Value& channels, const Json::Value& parameters);

private:
    PRegisterConfig GetRegisterConfig(const Json::Value& declaration);

    //! Read only by the register type or by the "readonly" key, like the readonly list of device/Load
    bool IsReadOnly(const Json::Value& declaration);

    PDeviceSetupItem CreateSetupItem(const Json::Value& declaration, const std::string& value);
};

typedef std::shared_ptr<TRPCDeviceSetRequest> PRPCDeviceSetRequest;

PRPCDeviceSetRequest ParseRPCDeviceSetRequest(const Json::Value& request,
                                              const TDeviceProtocolParams& protocolParams,
                                              PSerialDevice device,
                                              PDeviceTemplate deviceTemplate,
                                              bool deviceFromConfig,
                                              WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                              WBMQTT::TMqttRpcServer::TErrorCallback onError);

class TRPCDeviceSetSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceSetSerialClientTask(PRPCDeviceSetRequest request);
    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceSetRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};
