#include "rpc_port_handler.h"
#include "rpc_helpers.h"
#include "rpc_port_load_modbus_serial_client_task.h"
#include "rpc_port_load_raw_serial_client_task.h"
#include "rpc_port_scan_serial_client_task.h"
#include "rpc_port_setup_serial_client_task.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCPortHandler::TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                                 const std::string& requestPortSetupSchemaFilePath,
                                 const std::string& requestPortScanSchemaFilePath,
                                 PRPCConfig rpcConfig,
                                 TSerialClientTaskRunner& serialClientTaskRunner,
                                 TRPCDeviceParametersCache& parametersCache,
                                 WBMQTT::PMqttRpcServer rpcServer)
    : RequestPortLoadSchema(LoadRPCRequestSchema(requestPortLoadSchemaFilePath, "port/Load")),
      RequestPortSetupSchema(LoadRPCRequestSchema(requestPortSetupSchemaFilePath, "port/Setup")),
      RequestPortScanSchema(LoadRPCRequestSchema(requestPortScanSchemaFilePath, "port/Scan")),
      RPCConfig(rpcConfig),
      SerialClientTaskRunner(serialClientTaskRunner),
      ParametersCache(parametersCache)
{
    rpcServer->RegisterAsyncMethod("port",
                                   "Load",
                                   std::bind(&TRPCPortHandler::PortLoad,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterMethod("ports", "Load", std::bind(&TRPCPortHandler::LoadPorts, this, std::placeholders::_1));
    rpcServer->RegisterAsyncMethod("port",
                                   "Setup",
                                   std::bind(&TRPCPortHandler::PortSetup,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterAsyncMethod("port",
                                   "Scan",
                                   std::bind(&TRPCPortHandler::PortScan,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
}

void TRPCPortHandler::PortLoad(const Json::Value& request,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestPortLoadSchema);
    try {
        PSerialClientTask task;
        auto portResponseTimeout = RESPONSE_TIMEOUT_NOT_SET;
        auto serialClientParams = SerialClientTaskRunner.GetSerialClientParams(request);
        if (serialClientParams.SerialClient) {
            portResponseTimeout = serialClientParams.PortResponseTimeout;
        }
        auto responseTimeout = ParseResponseTimeout(request, portResponseTimeout);
        auto protocol = request.get("protocol", "raw").asString();
        if ((protocol == "modbus") || (protocol == "modbus-tcp")) {
            task = std::make_shared<TRPCPortLoadModbusSerialClientTask>(request,
                                                                        onResult,
                                                                        onError,
                                                                        ParametersCache,
                                                                        responseTimeout);
        } else {
            task = std::make_shared<TRPCPortLoadRawSerialClientTask>(request, onResult, onError, responseTimeout);
        }
        if (serialClientParams.SerialClient) {
            SerialClientTaskRunner.RunTask(request, task);
        } else {
            SerialClientTaskRunner.RunTaskOnExecutor(request, task);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortSetup(const Json::Value& request,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestPortSetupSchema);
    try {
        SerialClientTaskRunner.RunTask(request,
                                       std::make_shared<TRPCPortSetupSerialClientTask>(request, onResult, onError));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortScan(const Json::Value& request,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestPortScanSchema);
    try {
        PSerialClientTask task;
        auto responseTimeout = RESPONSE_TIMEOUT_NOT_SET;
        auto serialClientParams = SerialClientTaskRunner.GetSerialClientParams(request);
        if (serialClientParams.SerialClient) {
            responseTimeout = serialClientParams.PortResponseTimeout;
        }
        task = std::make_shared<TRPCPortScanSerialClientTask>(request, onResult, onError, responseTimeout);
        if (serialClientParams.SerialClient) {
            SerialClientTaskRunner.RunTask(request, task);
        } else {
            SerialClientTaskRunner.RunTaskOnExecutor(request, task);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCPortHandler::LoadPorts(const Json::Value& request)
{
    return RPCConfig->GetPortConfigs();
}
