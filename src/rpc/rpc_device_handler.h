#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      std::shared_ptr<TTemplateMap> templates,
                      PRPCConfig rpcConfig,
                      WBMQTT::PMqttRpcServer rpcServer,
                      PMQTTSerialDriver serialDriver);

private:
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    std::shared_ptr<TTemplateMap> Templates;
    PRPCConfig RPCConfig;
    TRPCPortDriverList PortDrivers;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
