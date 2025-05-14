#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      PTemplateMap templates,
                      PHandlerConfig handlerConfig,
                      TSerialClientTaskRunner& serialClientTaskRunner,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    PTemplateMap Templates;
    PHandlerConfig HandlerConfig;
    TSerialClientTaskRunner& SerialClientTaskRunner;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
