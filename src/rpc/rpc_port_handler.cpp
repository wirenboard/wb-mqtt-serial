#include "rpc_port_handler.h"
#include "rpc_helpers.h"
#include "rpc_port_load_handler.h"
#include "rpc_port_scan_handler.h"
#include "rpc_port_setup_handler.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCPortHandler::TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                                 const std::string& requestPortSetupSchemaFilePath,
                                 const std::string& requestPortScanSchemaFilePath,
                                 PRPCConfig rpcConfig,
                                 WBMQTT::PMqttRpcServer rpcServer,
                                 PMQTTSerialDriver serialDriver)
    : RPCConfig(rpcConfig)
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

    PortDrivers = std::make_shared<TRPCPortDriverList>(rpcConfig, serialDriver);
}

void TRPCPortHandler::PortLoad(const Json::Value& request,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        WBMQTT::JSON::Validate(request, RequestPortLoadSchema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    try {
        PRPCPortDriver rpcPortDriver = PortDrivers->Find(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCPortLoadHandler(request, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCPortLoadHandler(request, *port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortSetup(const Json::Value& request,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        PRPCPortSetupRequest rpcRequest = ParseRPCPortSetupRequest(request, RequestPortSetupSchema);
        PRPCPortDriver rpcPortDriver = PortDrivers->Find(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCPortSetupHandler(rpcRequest, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCPortSetupHandler(rpcRequest, *port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortScan(const Json::Value& request,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        WBMQTT::JSON::Validate(request, RequestPortScanSchema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    try {
        PRPCPortDriver rpcPortDriver = PortDrivers->Find(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCPortScanHandler(request, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCPortScanHandler(request, *port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCPortHandler::LoadPorts(const Json::Value& request)
{
    return RPCConfig->GetPortConfigs();
}
