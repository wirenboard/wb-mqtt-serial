#include "rpc_device_set_task.h"
#include "config_merge_template.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    void ExecRPCRequest(PPort port, PRPCDeviceSetRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        TDeviceSetupItems setupItems;
        rpcRequest->GetChannelsSetupItems(setupItems);
        rpcRequest->GetParametersSetupItems(setupItems);
        if (setupItems.empty()) {
            rpcRequest->OnResult(Json::Value(Json::objectValue));
        }

        std::string error;
        try {
            rpcRequest->Device->Prepare(*port, TDevicePrepareMode::WITHOUT_SETUP);
        } catch (const TSerialDeviceException& e) {
            error = std::string("Failed to prepare session: ") + e.what();
            LOG(Warn) << port->GetDescription() << " " << rpcRequest->Device->ToString() << ": " << error;
            throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        try {
            rpcRequest->Device->WriteSetupRegisters(*port, setupItems, true);
        } catch (const TSerialDeviceException& e) {
            error = e.what();
        }

        try {
            rpcRequest->Device->EndSession(*port);
        } catch (const TSerialDeviceException& e) {
            LOG(Warn) << port->GetDescription() << rpcRequest->Device->ToString()
                      << " unable to end session: " << e.what();
        }

        if (!error.empty()) {
            LOG(Warn) << port->GetDescription() << rpcRequest->Device->ToString() << ": " << error;
            throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        rpcRequest->OnResult(Json::Value(Json::objectValue));
    }
} // namespace

TRPCDeviceSetRequest::TRPCDeviceSetRequest(const TDeviceProtocolParams& protocolParams,
                                           PSerialDevice device,
                                           PDeviceTemplate deviceTemplate,
                                           bool deviceFromConfig)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig)
{}

void TRPCDeviceSetRequest::ParseRequestItems(const Json::Value& items,
                                             std::unordered_map<std::string, std::string>& map)
{
    for (auto it = items.begin(); it != items.end(); ++it) {
        map[it.key().asString()] = (*it).asString();
    }
}

void TRPCDeviceSetRequest::GetChannelsSetupItems(TDeviceSetupItems& setupItems)
{
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) {
        if (item["readonly"].asBool()) {
            continue;
        }
        auto id = item["name"].asString();
        if (Channels.count(id)) {
            setupItems.insert(CreateSetupItem(id, item, Channels[id]));
            Channels.erase(id);
        }
    }
    if (!Channels.empty()) {
        throw TRPCException("Channel \"" + Channels.begin()->first + "\" is read only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

void TRPCDeviceSetRequest::GetParametersSetupItems(TDeviceSetupItems& setupItems)
{
    auto params = DeviceTemplate->GetTemplate()["parameters"];
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto item = *it;
        if (item["readonly"].asBool()) {
            continue;
        }
        auto id = params.isObject() ? it.key().asString() : item["id"].asString();
        if (Parameters.count(id)) {
            setupItems.insert(CreateSetupItem(id, item, Parameters[id]));
            Parameters.erase(id);
        }
    }
    if (!Parameters.empty()) {
        throw TRPCException("Parameter \"" + Parameters.begin()->first + "\" is read only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

PDeviceSetupItem TRPCDeviceSetRequest::CreateSetupItem(const std::string& id,
                                                       const Json::Value& data,
                                                       const std::string& value)
{
    auto config = LoadRegisterConfig(data,
                                     *ProtocolParams.protocol->GetRegTypes(),
                                     std::string(),
                                     *ProtocolParams.factory,
                                     ProtocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                     0);
    auto itemConfig = std::make_shared<TDeviceSetupItemConfig>(id, config.RegisterConfig, value);
    return std::make_shared<TDeviceSetupItem>(itemConfig, Device);
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
    res->ParseRequestItems(request["channels"], res->Channels);
    res->ParseRequestItems(request["parameters"], res->Parameters);
    return res;
}

TRPCDeviceSetSerialClientTask::TRPCDeviceSetSerialClientTask(PRPCDeviceSetRequest request): Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceSetSerialClientTask::Run(PFeaturePort port,
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
