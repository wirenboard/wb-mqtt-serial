#include "rpc_port_handler.h"
#include "json_common.h"
#include "rpc_helpers.h"
#include "rpc_port_load_modbus_serial_client_task.h"
#include "rpc_port_load_raw_serial_client_task.h"
#include "rpc_port_scan_serial_client_task.h"
#include "rpc_port_setup_serial_client_task.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCPortHandler::TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                                 const std::string& requestPortSetupSchemaFilePath,
                                 const std::string& requestPortScanSchemaFilePath,
                                 TSerialDriverCore& serialDriverCore,
                                 TSerialClientTaskRunner& serialClientTaskRunner,
                                 WBMQTT::PMqttRpcServer rpcServer)
    : RequestPortLoadSchema(LoadRPCRequestSchema(requestPortLoadSchemaFilePath, "port/Load")),
      RequestPortSetupSchema(LoadRPCRequestSchema(requestPortSetupSchemaFilePath, "port/Setup")),
      RequestPortScanSchema(LoadRPCRequestSchema(requestPortScanSchemaFilePath, "port/Scan")),
      SerialDriverCore(serialDriverCore),
      SerialClientTaskRunner(serialClientTaskRunner)
{
    rpcServer->RegisterAsyncMethod("port",
                                   "Load",
                                   std::bind(&TRPCPortHandler::PortLoad,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterMethod("ports", "Load", std::bind(&TRPCPortHandler::LoadPorts, this, std::placeholders::_1));
    rpcServer->RegisterMethod("ports", "List", std::bind(&TRPCPortHandler::ListPorts, this, std::placeholders::_1));
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
        SerialClientTaskRunner.RunTask(
            request,
            [&](PSerialDevice deviceFromConfig, PDeviceParametersCache cache) -> PSerialClientTask {
                auto protocol = request.get("protocol", "raw").asString();
                if (deviceFromConfig) {
                    protocol = deviceFromConfig->Protocol()->GetName();
                }
                if ((protocol == "modbus") || (protocol == "modbus-tcp")) {
                    auto rpcRequest = ParseRPCPortLoadModbusRequest(request, cache);
                    rpcRequest->OnResult = onResult;
                    rpcRequest->OnError = onError;
                    if (deviceFromConfig) {
                        rpcRequest->SlaveId = GetModbusSlaveId(*deviceFromConfig);
                        rpcRequest->Protocol = protocol;
                    }
                    return std::make_shared<TRPCPortLoadModbusSerialClientTask>(rpcRequest);
                }
                if (protocol != "raw") {
                    throw TRPCException("The device's protocol is not supported",
                                        TRPCResultCode::RPC_WRONG_PARAM_VALUE);
                }
                return std::make_shared<TRPCPortLoadRawSerialClientTask>(request, onResult, onError);
            });
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
        SerialClientTaskRunner.RunTask(request, [&](PSerialDevice, PDeviceParametersCache) {
            return std::make_shared<TRPCPortSetupSerialClientTask>(request, onResult, onError);
        });
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
        SerialClientTaskRunner.RunTask(request, [&](PSerialDevice, PDeviceParametersCache) {
            return std::make_shared<TRPCPortScanSerialClientTask>(request, onResult, onError);
        });
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCPortHandler::LoadPorts(const Json::Value& request)
{
    auto handlerConfig = SerialDriverCore.GetConfig();
    if (!handlerConfig) {
        return Json::Value(Json::arrayValue);
    }
    return MakePortConfigsResponse(*handlerConfig);
}

Json::Value TRPCPortHandler::ListPorts(const Json::Value& request)
{
    auto handlerConfig = SerialDriverCore.GetConfig();
    if (!handlerConfig) {
        Json::Value res;
        MakeArray("ports", res);
        return res;
    }
    return MakePortsListResponse(*handlerConfig);
}
