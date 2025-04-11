#include "rpc_device_load_config_task.h"
#include "rpc_helpers.h"
#include "serial_port.h"

#define LOG(logger) ::logger.Log() << "[TEST] "

PRPCDeviveLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request)
{
    PRPCDeviveLoadConfigRequest res = std::make_shared<TRPCDeviveLoadConfigRequest>();
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    WBMQTT::JSON::Get(request, "slave_id", res->SlaveId);
    WBMQTT::JSON::Get(request, "devive_type", res->DeviceType);
    return res;
}

void ExecRPCDeviveLoadConfigRequest(TPort& port, PRPCDeviveLoadConfigRequest rpcRequest)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    port.SkipNoise();

    Json::Value replyJSON;
    replyJSON["dummy"] = true;
    rpcRequest->OnResult(replyJSON);

    LOG(Info) << "Hello there!";
}

TRPCDeviceLoadConfigSerialClientTask::TRPCDeviceLoadConfigSerialClientTask(PRPCDeviveLoadConfigRequest request)
    : Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadConfigSerialClientTask::Run(
    PPort port,
    TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        lastAccessedDevice.PrepareToAccess(nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCDeviveLoadConfigRequest(*port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }

    return ISerialClientTask::TRunResult::OK;
}
