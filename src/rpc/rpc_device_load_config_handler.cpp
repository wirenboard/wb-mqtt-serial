#include "rpc_device_load_config_handler.h"
#include "rpc_device_load_config_task.h"
#include "serial_port.h"
#include "tcp_port.h"

namespace
{
    void SetCallbacks(TRPCDeviveLoadConfigRequest& request,
                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
    {
        request.OnResult = onResult;
        request.OnError = onError;
    }
} // namespace

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                PSerialClient serialClient,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (!serialClient) {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
    auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request);
    SetCallbacks(*rpcRequest, onResult, onError);
    serialClient->AddTask(std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
}

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                TPort& port,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request);
    SetCallbacks(*rpcRequest, onResult, onError);
    ExecRPCDeviveLoadConfigRequest(port, rpcRequest);
}
