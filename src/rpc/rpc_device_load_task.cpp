#include "rpc_device_load_task.h"
#include "config_merge_template.h"
#include "port/serial_port.h"
#include "rpc_helpers.h"
#include "wb_registers.h"

namespace
{
    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }
        Json::Value result(Json::objectValue);
        for (int i = 0; i < 2; i++) {
            auto registerList = i ? rpcRequest->GetParametersRegisterList() : rpcRequest->GetChannelsRegisterList();
            if (!registerList.empty()) {
                Json::Value data(Json::objectValue);
                ReadRegisterList(*port, rpcRequest->Device, registerList);
                for (const auto& item: registerList) {
                    data[item.Id] = RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue());
                }
                result[i ? "parameters" : "channels"] = data;
            }
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

void TRPCDeviceLoadRequest::ParseRequestItems(const Json::Value& items, std::list<std::string>& list)
{
    for (const auto& item: items) {
        auto id = item.asString();
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            list.push_back(id);
        }
    }
}

TRPCRegisterList TRPCDeviceLoadRequest::GetChannelsRegisterList()
{
    Json::Value items(Json::arrayValue);
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) {
        if (item["address"].isNull()) { // write only channel
            continue;
        }
        auto id = item["name"].asString();
        if (std::find(Channels.begin(), Channels.end(), id) != Channels.end()) {
            item["id"] = id;
            items.append(item);
            Channels.remove(id);
        }
    }
    if (!Channels.empty()) {
        throw TRPCException("Channel \"" + Channels.front() + "\" is write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return CreateRegisterList(ProtocolParams, Device, items);
}

TRPCRegisterList TRPCDeviceLoadRequest::GetParametersRegisterList()
{
    auto params = DeviceTemplate->GetTemplate()["parameters"];
    Json::Value items(Json::arrayValue);
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto item = *it;
        if (item["address"].isNull()) { // write only parameter
            continue;
        }
        auto id = params.isObject() ? it.key().asString() : item["id"].asString();
        if (std::find(Parameters.begin(), Parameters.end(), id) != Parameters.end()) {
            if (params.isObject()) {
                item["id"] = id;
            }
            items.append(item);
            Parameters.remove(id);
        }
    }
    if (!Channels.empty()) {
        throw TRPCException("Parameter \"" + Parameters.front() + "\" is write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return CreateRegisterList(ProtocolParams, Device, items);
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
    res->ParseRequestItems(request["channels"], res->Channels);
    res->ParseRequestItems(request["parameters"], res->Parameters);
    return res;
}

TRPCDeviceLoadSerialClientTask::TRPCDeviceLoadSerialClientTask(PRPCDeviceLoadRequest request): Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadSerialClientTask::Run(PFeaturePort port,
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
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        if (!Request->DeviceFromConfig) {
            TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
            ExecRPCRequest(port, Request);
        } else {
            ExecRPCRequest(port, Request);
        }
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
