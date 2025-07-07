#pragma once
#include "rpc_device_handler.h"

class TRPCDeviceSetRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                         PSerialDevice device,
                         PDeviceTemplate deviceTemplate,
                         bool deviceFromConfig);

    TDeviceSetupItems SetupItems;

    void ParseChannels(const Json::Value& request);
    void ParseParameters(const Json::Value& request);

private:
    void AddSetupItem(const std::string& id, const Json::Value& data, const std::string& value);
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
    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceSetRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};
