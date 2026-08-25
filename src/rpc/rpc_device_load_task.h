#pragma once
#include "rpc_device_handler.h"

class TRPCDeviceLoadRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceLoadRequest(const TDeviceProtocolParams& protocolParams,
                          PSerialDevice device,
                          PDeviceTemplate deviceTemplate,
                          bool deviceFromConfig);

    std::set<std::string> Channels;
    std::set<std::string> Parameters;

    void ParseRequestItems(const Json::Value& items, std::set<std::string>& list);

    //! Registers to read: parameters referenced by the conditions of the requested channels and parameters
    TRPCRegisterList GetConditionParametersRegisterList();

    /**
     * @brief Builds the register list of the requested channels: only declarations with a true condition
     *        are read. Conditions over parameters missing from conditionParams are evaluated with
     *        an undefined value, a channel with false conditions is dropped. A null conditionParams
     *        skips the condition evaluation.
     */
    TRPCRegisterList GetChannelsRegisterList(const Json::Value& conditionParams = Json::Value());

    /**
     * @brief Builds the register list of the requested parameters: the declaration with a true condition
     *        acts and defines the value conversion. Conditions over parameters missing from
     *        conditionParams are evaluated with an undefined value, a parameter with false conditions
     *        is dropped. A parameter already read for condition evaluation is put into data instead
     *        of the list. A null conditionParams skips the condition evaluation.
     *        A chain of fw variants of a parameter (the same condition, pairwise different "fw")
     *        is not ambiguous, CreateParametersRegisterList merges it into one register.
     *
     * @throws TRPCException if several declarations of a parameter match at once
     */
    TRPCRegisterList GetParametersRegisterList(const Json::Value& conditionParams = Json::Value(),
                                               Json::Value* data = nullptr);
};

typedef std::shared_ptr<TRPCDeviceLoadRequest> PRPCDeviceLoadRequest;

PRPCDeviceLoadRequest ParseRPCDeviceLoadRequest(const Json::Value& request,
                                                const TDeviceProtocolParams& protocolParams,
                                                PSerialDevice device,
                                                PDeviceTemplate deviceTemplate,
                                                bool deviceFromConfig,
                                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                WBMQTT::TMqttRpcServer::TErrorCallback onError);

class TRPCDeviceLoadSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceLoadSerialClientTask(PRPCDeviceLoadRequest request);
    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceLoadRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};
