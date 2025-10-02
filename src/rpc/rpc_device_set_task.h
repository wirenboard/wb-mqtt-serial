#pragma once
#include "rpc_device_handler.h"

class TRPCDeviceSetRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                         PSerialDevice device,
                         PDeviceTemplate deviceTemplate,
                         bool deviceFromConfig);

    std::unordered_map<std::string, std::string> Channels;
    std::unordered_map<std::string, std::string> Parameters;

    void ParseRequestItems(const Json::Value& items, std::unordered_map<std::string, std::string>& map);

    void GetChannelsSetupItems(TDeviceSetupItems& setupItems);
    void GetParametersSetupItems(TDeviceSetupItems& setupItems);

private:
    PDeviceSetupItem CreateSetupItem(const std::string& id, const Json::Value& data, const std::string& value);
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
