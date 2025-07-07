#include "rpc_device_load_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"
#include "serial_port.h"
#include "wb_registers.h"

// TODO: remove it
#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }
        Json::Value result;
        if (!rpcRequest->ClannelRegisterList.empty()) {
            ReadRegisterList(rpcRequest->Device, rpcRequest->ClannelRegisterList, result["channels"]);
        }
        if (!rpcRequest->ParameterRegisterList.empty()) {
            ReadRegisterList(rpcRequest->Device, rpcRequest->ParameterRegisterList, result["parameters"]);
        }
        rpcRequest->OnResult(result);
    }
} // namespace

TRPCDeviceLoadRequest::TRPCDeviceLoadRequest(const TDeviceProtocolParams& protocolParams,
                                             PSerialDevice device,
                                             PDeviceTemplate deviceTemplate,
                                             bool deviceFromConfig)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig)
{}

void TRPCDeviceLoadRequest::ParseChannels(const Json::Value& request)
{
    std::list<std::string> list;
    for (const auto& item: request["channels"]) {
        auto id = item.asString();
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            list.push_back(id);
        }
    }
    Json::Value items(Json::arrayValue);
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) { // TODO: check if write only
        auto id = item["name"].asString();
        if (std::find(list.begin(), list.end(), id) != list.end()) {
            item["id"] = id;
            items.append(item);
        }
    }
    ClannelRegisterList = CreateRegisterList(ProtocolParams, Device, items);
}

void TRPCDeviceLoadRequest::ParseParameters(const Json::Value& request)
{
    std::list<std::string> list;
    for (const auto& item: request["parameters"]) {
        auto id = item.asString();
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            list.push_back(id);
        }
    }
    auto parameters = DeviceTemplate->GetTemplate()["parameters"];
    Json::Value items(Json::arrayValue);
    for (auto it = parameters.begin(); it != parameters.end(); ++it) { // TODO: check if write only
        auto item = *it;
        auto id = parameters.isObject() ? it.key().asString() : item["id"].asString();
        if (std::find(list.begin(), list.end(), id) != list.end()) {
            if (parameters.isObject()) {
                item["id"] = id;
            }
            items.append(item);
        }
    }
    ParameterRegisterList = CreateRegisterList(ProtocolParams, Device, items);
}

PRPCDeviceLoadRequest ParseRPCDeviceLoadRequest(const Json::Value& request,
                                                const TDeviceProtocolParams& protocolParams,
                                                PSerialDevice device,
                                                PDeviceTemplate deviceTemplate,
                                                bool deviceFromConfig,
                                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto res = std::make_shared<TRPCDeviceLoadRequest>(protocolParams, device, deviceTemplate, deviceFromConfig);
    res->ParseSettings(request, onResult, onError);
    res->ParseChannels(request);
    res->ParseParameters(request);
    return res;
}

TRPCDeviceLoadSerialClientTask::TRPCDeviceLoadSerialClientTask(PRPCDeviceLoadRequest request): Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadSerialClientTask::Run(PPort port,
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
