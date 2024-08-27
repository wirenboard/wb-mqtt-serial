#include "rpc_port_load_handler.h"
#include "rpc_port_handler.h"
#include "rpc_port_load_modbus_serial_client_task.h"
#include "rpc_port_load_raw_serial_client_task.h"
#include "serial_port.h"
#include "tcp_port.h"

namespace
{
    void SetCallbacks(TRPCPortLoadRequest& request,
                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
    {

        request.OnResult = onResult;
        request.OnError = onError;
    }
} // namespace

void RPCPortLoadHandler(const Json::Value& request,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (!serialClient) {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
    auto protocol = request.get("protocol", "raw").asString();
    if (protocol == "modbus") {
        auto rpcRequest = ParseRPCPortLoadModbusRequest(request);
        SetCallbacks(*rpcRequest, onResult, onError);
        serialClient->AddTask(std::make_shared<TRPCPortLoadModbusSerialClientTask>(rpcRequest));
        return;
    }
    auto rpcRequest = ParseRPCPortLoadRawRequest(request);
    SetCallbacks(*rpcRequest, onResult, onError);
    serialClient->AddTask(std::make_shared<TRPCPortLoadRawSerialClientTask>(rpcRequest));
}

void RPCPortLoadHandler(const Json::Value& request,
                        PPort port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    Json::Value replyJSON;
    auto protocol = request.get("protocol", "raw").asString();
    if (protocol == "modbus") {
        auto rpcRequest = ParseRPCPortLoadModbusRequest(request);
        auto response = ExecRPCPortLoadModbusRequest(port, rpcRequest);
        replyJSON["response"] = FormatResponse(response, rpcRequest->Format);
    } else {
        auto rpcRequest = ParseRPCPortLoadRawRequest(request);
        auto response = ExecRPCPortLoadRawRequest(port, rpcRequest);
        replyJSON["response"] = FormatResponse(response, rpcRequest->Format);
    }
    onResult(replyJSON);
}
