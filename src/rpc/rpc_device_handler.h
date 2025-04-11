#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      std::shared_ptr<TTemplateMap> templates,
                      PRPCConfig rpcConfig,
                      WBMQTT::PMqttRpcServer rpcServer,
                      PMQTTSerialDriver serialDriver);

private:
    Json::Value RequestDeviceLoadConfigSchema;
    std::shared_ptr<TTemplateMap> Templates;
    PRPCConfig RPCConfig;
    PRPCPortDriverList PortDrivers;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
