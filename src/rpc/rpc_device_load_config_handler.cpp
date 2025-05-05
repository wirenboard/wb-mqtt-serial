#include "rpc_device_load_config_handler.h"
#include "rpc_device_load_config_task.h"
#include "rpc_exception.h"
#include "serial_port.h"
#include "tcp_port.h"

namespace
{
    void SetCallbacks(TRPCDeviceLoadConfigRequest& request,
                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
    {
        request.OnResult = onResult;
        request.OnError = onError;
    }
} // namespace

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const TSerialDeviceFactory& deviceFactory,
                                PDeviceTemplate deviceTemplate,
                                PSerialDevice device,
                                PSerialClient serialClient,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (!serialClient) {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
    auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request, deviceFactory, deviceTemplate, device);
    SetCallbacks(*rpcRequest, onResult, onError);
    serialClient->AddTask(std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
}

void RPCDeviceLoadConfigHandler(const Json::Value& request,
                                const TSerialDeviceFactory& deviceFactory,
                                PDeviceTemplate deviceTemplate,
                                PSerialDevice device,
                                PPort port,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request, deviceFactory, deviceTemplate, device);
    SetCallbacks(*rpcRequest, onResult, onError);
    ExecRPCDeviceLoadConfigRequest(port, rpcRequest);
}
