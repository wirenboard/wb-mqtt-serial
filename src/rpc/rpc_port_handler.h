#pragma once
#include "rpc_device_handler.h"
#include "rpc_port_driver_list.h"

class TRPCPortHandler
{
public:
    TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                    const std::string& requestPortSetupSchemaFilePath,
                    const std::string& requestPortScanSchemaFilePath,
                    PHandlerConfig handlerConfig,
                    TSerialClientTaskRunner& serialClientTaskRunner,
                    TRPCDeviceParametersCache& parametersCache,
                    WBMQTT::PMqttRpcServer rpcServer);

private:
    Json::Value RequestPortLoadSchema;
    Json::Value RequestPortSetupSchema;
    Json::Value RequestPortScanSchema;
    PHandlerConfig HandlerConfig;
    TSerialClientTaskRunner& SerialClientTaskRunner;
    TRPCDeviceParametersCache& ParametersCache;

    void PortLoad(const Json::Value& request,
                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                  WBMQTT::TMqttRpcServer::TErrorCallback onError);
    void PortSetup(const Json::Value& request,
                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                   WBMQTT::TMqttRpcServer::TErrorCallback onError);
    void PortScan(const Json::Value& request,
                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                  WBMQTT::TMqttRpcServer::TErrorCallback onError);
    Json::Value LoadPorts(const Json::Value& request);
    Json::Value ListPorts(const Json::Value& request);
};

typedef std::shared_ptr<TRPCPortHandler> PRPCPortHandler;
