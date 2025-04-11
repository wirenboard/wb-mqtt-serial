#pragma once
#include "rpc_port_driver_list.h"

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      PRPCConfig rpcConfig,
                      WBMQTT::PMqttRpcServer rpcServer,
                      PMQTTSerialDriver serialDriver);

private:
    Json::Value RequestDeviceLoadConfigSchema;
    PRPCConfig RPCConfig;
    PRPCPortDriverList PortDrivers;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
