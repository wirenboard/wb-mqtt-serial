#include "rpc_port_scan_handler.h"
#include "rpc_port_handler.h"
#include "rpc_port_scan_serial_client_task.h"

namespace
{
    void SetCallbacks(TRPCPortScanRequest& request,
                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
    {

        request.OnResult = onResult;
        request.OnError = onError;
    }
} // namespace

void RPCPortScanHandler(const Json::Value& request,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (!serialClient) {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
    auto rpcRequest = ParseRPCPortScanRequest(request);
    SetCallbacks(*rpcRequest, onResult, onError);
    serialClient->AddTask(std::make_shared<TRPCPortScanSerialClientTask>(rpcRequest));
}

void RPCPortScanHandler(const Json::Value& request,
                        TPort& port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto rpcRequest = ParseRPCPortScanRequest(request);
    SetCallbacks(*rpcRequest, onResult, onError);
    ExecRPCPortScanRequest(port, rpcRequest);
}
