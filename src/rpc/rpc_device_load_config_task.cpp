#include "rpc_device_load_config_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"
#include "serial_config.h"
#include "serial_port.h"
#include <string>

TRPCDeviveLoadConfigRequest::TRPCDeviveLoadConfigRequest(const Json::Value& parameters,
                                                         const TSerialDeviceFactory& deviceFactory)
    : Parameters(parameters),
      DeviceFactory(deviceFactory)
{}

PRPCDeviveLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const Json::Value& parameters,
                                                            const TSerialDeviceFactory& deviceFactory)
{
    PRPCDeviveLoadConfigRequest res = std::make_shared<TRPCDeviveLoadConfigRequest>(parameters, deviceFactory);
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);

    // TODO: add default timeout values?
    WBMQTT::JSON::Get(request, "response_timeout", res->ResponseTimeout);
    WBMQTT::JSON::Get(request, "frame_timeout", res->FrameTimeout);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);

    WBMQTT::JSON::Get(request, "slave_id", res->SlaveId);
    WBMQTT::JSON::Get(request, "device_type", res->DeviceType);
    return res;
}

void ExecRPCDeviveLoadConfigRequest(TPort& port, PRPCDeviveLoadConfigRequest rpcRequest)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    port.SkipNoise();

    // TODO: refactor protocol name
    const TDeviceProtocolParams& protocolParams = rpcRequest->DeviceFactory.GetProtocolParams("modbus");
    Modbus::TModbusRTUTraits traits;
    Json::Value configData;

    for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
        const Json::Value& registerData = *it;
        if (registerData["readonly"].asInt() != 0) {
            continue;
        }
        auto config = LoadRegisterConfig(registerData,
                                         *protocolParams.protocol->GetRegTypes(),
                                         std::string(), // TODO: add some string?
                                         *protocolParams.factory,
                                         protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                         0);
        auto res = Modbus::ReadRegister(traits,
                                        port,
                                        rpcRequest->SlaveId,
                                        *config.RegisterConfig,
                                        std::chrono::microseconds(0),
                                        rpcRequest->ResponseTimeout,
                                        rpcRequest->FrameTimeout);
        configData[it.key().asString()] = res.Get<uint64_t>(); // TODO: use other types?
    }

    TJsonParams jsonParams(configData);
    TExpressionsCache expressionsCache;
    for (auto it = rpcRequest->Parameters.begin(); it != rpcRequest->Parameters.end(); ++it) {
        if (!CheckCondition(*it, jsonParams, &expressionsCache)) {
            configData.removeMember(it.key().asString());
        }
    }

    rpcRequest->OnResult(configData);
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
