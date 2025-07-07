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

        rpcRequest->Device->Prepare(TDevicePrepareMode::WITHOUT_SETUP);
        rpcRequest->Device->WriteSetupRegisters(rpcRequest->SetupItems);
        rpcRequest->Device->EndSession();

        rpcRequest->OnResult(Json::Value());
    }
} // namespace

TRPCDeviceSetRequest::TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                                           PSerialDevice device,
                                           PDeviceTemplate deviceTemplate,
                                           bool deviceFromConfig)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig)
{}

void TRPCDeviceSetRequest::AddSetupItem(const std::string& id, const Json::Value& data, const std::string& value)
{
    auto config = LoadRegisterConfig(data,
                                     *ProtocolParams.protocol->GetRegTypes(),
                                     std::string(),
                                     *ProtocolParams.factory,
                                     ProtocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                     0);
    auto itemConfig = std::make_shared<TDeviceSetupItemConfig>(id, config.RegisterConfig, value);
    SetupItems.insert(std::make_shared<TDeviceSetupItem>(itemConfig, Device));
}

void TRPCDeviceSetRequest::ParseChannels(const Json::Value& request)
{
    std::unordered_map<std::string, std::string> map;
    auto channels = request["channels"];
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        map[it.key().asString()] = (*it).asString();
    }
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) { // TODO: check if read only
        auto id = item["name"].asString();
        if (map.count(id)) {
            AddSetupItem(id, item, map[id]);
            map.erase(id);
        }
    }
}

void TRPCDeviceSetRequest::ParseParameters(const Json::Value& request)
{
    std::unordered_map<std::string, std::string> map;
    auto requestParams = request["parameters"];
    for (auto it = requestParams.begin(); it != requestParams.end(); ++it) {
        map[it.key().asString()] = (*it).asString();
    }
    auto templateParams = DeviceTemplate->GetTemplate()["parameters"];
    for (auto it = templateParams.begin(); it != templateParams.end(); ++it) { // TODO: check if read only
        auto item = *it;
        auto id = templateParams.isObject() ? it.key().asString() : item["id"].asString();
        if (map.count(id)) {
            AddSetupItem(id, item, map[id]);
            map.erase(id);
        }
    }
}

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
    res->ParseChannels(request);
    res->ParseParameters(request);
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
