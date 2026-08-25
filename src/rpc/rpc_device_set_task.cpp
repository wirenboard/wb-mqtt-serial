#include "rpc_device_set_task.h"
#include "config_merge_template.h"
#include "expression_evaluator.h"
#include "rpc_helpers.h"

#include <set>

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    void ExecRPCRequest(PPort port, PRPCDeviceSetRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        // 1. Template declarations of the requested channels and parameters, every condition variant
        auto channelDeclarations = rpcRequest->GetChannelDeclarations();
        auto parameterDeclarations = rpcRequest->GetParameterDeclarations();
        if (channelDeclarations.empty() && parameterDeclarations.empty()) {
            rpcRequest->OnResult(Json::Value(Json::objectValue));
            return;
        }

        PrepareSession(*port, rpcRequest->Device);

        std::string error;
        try {
            // 2. The request parameters are the condition inputs, the missing condition parameters are read
            Json::Value conditionValues(rpcRequest->Parameters);
            auto registerList =
                rpcRequest->GetConditionParametersRegisterList(channelDeclarations, parameterDeclarations);
            ReadRegisterList(*port, rpcRequest->Device, registerList);
            MarkUnsupportedRegisterItems(*port, *rpcRequest, registerList);
            MergeRegisterListValues(registerList, conditionValues);

            // 3. The declaration with a true condition acts, an item without one is skipped
            auto channels = rpcRequest->SelectActingDeclarations("Channel", channelDeclarations, conditionValues);
            auto parameters = rpcRequest->SelectActingDeclarations("Parameter", parameterDeclarations, conditionValues);

            // 4. Write
            rpcRequest->Device->WriteSetupRegisters(*port, rpcRequest->CreateSetupItems(channels, parameters), true);
        } catch (const TSerialDeviceException& e) {
            error = e.what();
        } catch (...) {
            EndSession(*port, rpcRequest->Device);
            throw;
        }

        EndSession(*port, rpcRequest->Device);

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

Json::Value TRPCDeviceSetRequest::GetChannelDeclarations()
{
    Json::Value notFound(Channels);
    Json::Value declarations(Json::arrayValue);
    for (const auto& item: DeviceTemplate->GetTemplate()["channels"]) {
        auto name = item["name"].asString();
        if (!Channels.isMember(name) || IsReadOnly(item)) {
            continue;
        }
        notFound.removeMember(name);
        auto declaration = item;
        declaration["id"] = name;
        declarations.append(declaration);
    }
    if (!notFound.empty()) {
        throw TRPCException("Channel \"" + notFound.getMemberNames().front() + "\" is read only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return declarations;
}

Json::Value TRPCDeviceSetRequest::GetParameterDeclarations()
{
    const auto& params = DeviceTemplate->GetTemplate()["parameters"];
    Json::Value declarations(Json::arrayValue);
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto id = params.isObject() ? it.key().asString() : (*it)["id"].asString();
        // read only declarations are never written, so their conditions are not evaluated
        if ((*it)["readonly"].asBool() || !Parameters.isMember(id)) {
            continue;
        }
        auto item = *it;
        item["id"] = id;
        declarations.append(item);
    }
    return declarations;
}

TRPCRegisterList TRPCDeviceSetRequest::GetConditionParametersRegisterList(const Json::Value& channels,
                                                                          const Json::Value& parameters)
{
    std::set<std::string> neededParams;
    Expressions::TExpressionsCache exprCache;
    for (const auto* declarations: {&channels, &parameters}) {
        for (const auto& item: *declarations) {
            auto deps = Expressions::GetDependencies(item["condition"].asString(), exprCache);
            neededParams.insert(deps.begin(), deps.end());
        }
    }
    for (const auto& name: Parameters.getMemberNames()) {
        neededParams.erase(name);
    }
    if (neededParams.empty()) {
        return {};
    }
    return CreateParametersRegisterList(ProtocolParams,
                                        Device,
                                        DeviceTemplate->GetTemplate()["parameters"],
                                        Json::Value(),
                                        neededParams);
}

Json::Value TRPCDeviceSetRequest::SelectActingDeclarations(const std::string& kind,
                                                           const Json::Value& declarations,
                                                           const Json::Value& conditionValues)
{
    TJsonParams exprParams(conditionValues);
    Expressions::TExpressionsCache exprCache;
    std::set<std::string> matched;
    Json::Value acting(Json::arrayValue);
    for (const auto& item: declarations) {
        if (!CheckCondition(item, exprParams, &exprCache)) {
            continue;
        }
        auto id = item["id"].asString();
        // a template error, config validation reports it as a duplicate definition
        if (!matched.emplace(id).second) {
            throw TRPCException(kind + " \"" + id +
                                    "\" is ambiguous: several declarations match the condition parameter values",
                                TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }
        acting.append(item);
    }
    return acting;
}

TDeviceSetupItems TRPCDeviceSetRequest::CreateSetupItems(const Json::Value& channels, const Json::Value& parameters)
{
    TDeviceSetupItems setupItems;
    for (const auto& item: channels) {
        setupItems.insert(CreateSetupItem(item, Channels[item["id"].asString()].asString()));
    }
    for (const auto& item: parameters) {
        setupItems.insert(CreateSetupItem(item, Parameters[item["id"].asString()].asString()));
    }
    return setupItems;
}

PRegisterConfig TRPCDeviceSetRequest::GetRegisterConfig(const Json::Value& declaration)
{
    return LoadRegisterConfig(declaration,
                              *ProtocolParams.protocol->GetRegTypes(),
                              std::string(),
                              *ProtocolParams.factory,
                              ProtocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                              0)
        .RegisterConfig;
}

bool TRPCDeviceSetRequest::IsReadOnly(const Json::Value& declaration)
{
    return GetRegisterConfig(declaration)->AccessType == TRegisterConfig::EAccessType::READ_ONLY;
}

PDeviceSetupItem TRPCDeviceSetRequest::CreateSetupItem(const Json::Value& declaration, const std::string& value)
{
    auto itemConfig =
        std::make_shared<TDeviceSetupItemConfig>(declaration["id"].asString(), GetRegisterConfig(declaration), value);
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
    res->Channels = request["channels"];
    res->Parameters = request["parameters"];
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
