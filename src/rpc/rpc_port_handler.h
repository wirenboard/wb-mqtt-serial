#pragma once
#include "rpc_device_handler.h"
#include "rpc_port_driver_list.h"

const std::chrono::seconds DefaultRPCTotalTimeout(10);

class TRPCPortHandler
{
public:
    TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                    const std::string& requestPortSetupSchemaFilePath,
                    const std::string& requestPortScanSchemaFilePath,
                    PRPCConfig rpcConfig,
                    TSerialClientTaskRunner& serialClientTaskRunner,
                    TRPCDeviceParametersCache& parametersCache,
                    WBMQTT::PMqttRpcServer rpcServer);

private:
    Json::Value RequestPortLoadSchema;
    Json::Value RequestPortSetupSchema;
    Json::Value RequestPortScanSchema;
    PRPCConfig RPCConfig;
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
};

typedef std::shared_ptr<TRPCPortHandler> PRPCPortHandler;
