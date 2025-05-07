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
                                 WBMQTT::PMqttRpcServer rpcServer)
    : RPCConfig(rpcConfig),
      SerialClientTaskRunner(serialClientTaskRunner)
{
    try {
        RequestPortLoadSchema = WBMQTT::JSON::Parse(requestPortLoadSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC port/Load request schema reading error: " << e.what();
        throw;
    }

    try {
        RequestPortSetupSchema = WBMQTT::JSON::Parse(requestPortSetupSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC port/Setup request schema reading error: " << e.what();
        throw;
    }

    try {
        RequestPortScanSchema = WBMQTT::JSON::Parse(requestPortScanSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC port/Scan request schema reading error: " << e.what();
        throw;
    }

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
    ValidateRequest(request, RequestPortLoadSchema);
    try {
        auto protocol = request.get("protocol", "raw").asString();
        if (protocol == "modbus") {
            SerialClientTaskRunner.RunTask(
                request,
                std::make_shared<TRPCPortLoadModbusSerialClientTask>(request, onResult, onError));
        } else {
            SerialClientTaskRunner.RunTask(
                request,
                std::make_shared<TRPCPortLoadRawSerialClientTask>(request, onResult, onError));
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortSetup(const Json::Value& request,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRequest(request, RequestPortSetupSchema);
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
    ValidateRequest(request, RequestPortScanSchema);
    try {
        SerialClientTaskRunner.RunTask(request,
                                       std::make_shared<TRPCPortScanSerialClientTask>(request, onResult, onError));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCPortHandler::LoadPorts(const Json::Value& request)
{
    return RPCConfig->GetPortConfigs();
}

void TRPCPortHandler::ValidateRequest(const Json::Value& request, const Json::Value& schema)
{
    try {
        WBMQTT::JSON::Validate(request, schema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}
