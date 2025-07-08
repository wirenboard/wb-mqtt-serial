#pragma once
#include "rpc_device_handler.h"

class TRPCDeviceLoadRequest: public TRPCDeviceRequest
{
public:
    TRPCDeviceLoadRequest(const TDeviceProtocolParams& protocolParams,
                          PSerialDevice device,
                          PDeviceTemplate deviceTemplate,
                          bool deviceFromConfig);

    std::list<std::string> Channels;
    std::list<std::string> Parameters;

    void ParseRequestItems(const Json::Value& items, std::list<std::string>& list);

    TRPCRegisterList GetChannelsRegisterList();
    TRPCRegisterList GetParametersRegisterList();
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
    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCDeviceLoadRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};
