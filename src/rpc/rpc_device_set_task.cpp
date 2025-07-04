#include "rpc_device_set_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"
#include "serial_port.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    void ExecRPCRequest(PPort port, PRPCDeviceSetRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        Json::Value result;
        result["dummySet"] = true;
        rpcRequest->OnResult(result);
    }
} // namespace

TRPCDeviceSetRequest::TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                                           PSerialDevice device,
                                           PDeviceTemplate deviceTemplate,
                                           bool deviceFromConfig)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig)
{}

PRPCDeviceSetRequest ParseRPCDeviceSetRequest(const Json::Value& request,
                                              const TDeviceProtocolParams& protocolParams,
                                              PSerialDevice device,
                                              PDeviceTemplate deviceTemplate,
                                              bool deviceFromConfig,
                                              WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                              WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto res = std::make_shared<TRPCDeviceSetRequest>(protocolParams, device, deviceTemplate, deviceFromConfig);
    res->ParseSettings(request, onResult, onError);
    auto channels = request["channels"];
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        res->Channels[it.key().asString()] = (*it).asString();
    }
    auto parameters = request["parameters"];
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        res->Parameters[it.key().asString()] = (*it).asString();
    }
    return res;
}

TRPCDeviceSetSerialClientTask::TRPCDeviceSetSerialClientTask(PRPCDeviceSetRequest request): Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceSetSerialClientTask::Run(PPort port,
                                                                 TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                                 const std::list<PSerialDevice>& polledDevices)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }
    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(nullptr);
        if (!Request->DeviceFromConfig) {
            TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        }
        ExecRPCRequest(port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
