#include "rpc_device_load_task.h"
#include "config_merge_template.h"
#include "port/serial_port.h"
#include "rpc_helpers.h"

namespace
{
    void ReadRegisters(PPort port, PRPCDeviceLoadRequest rpcRequest, TRPCRegisterList& registerList, Json::Value& data)
    {
        ReadRegisterList(*port, rpcRequest->Device, registerList);
        for (const auto& item: registerList) {
            data[item.Id] = RegisterGotValue(item)
                                ? RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue())
                                : UNSUPPORTED_VALUE;
        }
        MarkUnsupportedRegisterItems(*port, *rpcRequest, registerList, &data);
    }

    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        PrepareSession(*port, rpcRequest->Device);

        Json::Value result(Json::objectValue);
        try {
            // Step 1: Read parameters that are referenced by conditions of the requested items.
            Json::Value conditionParamValues(Json::objectValue);
            auto condParamRegList = rpcRequest->GetConditionParametersRegisterList();
            ReadRegisterList(*port, rpcRequest->Device, condParamRegList);
            MarkUnsupportedRegisterItems(*port, *rpcRequest, condParamRegList);
            MergeRegisterListValues(condParamRegList, conditionParamValues);

            // Step 2: Read channels, filtering by condition using actual parameter values
            Json::Value readonlyList(Json::arrayValue);
            Json::Value channelData(Json::objectValue);
            auto channelRegList = rpcRequest->GetChannelsRegisterList(conditionParamValues);
            ReadRegisters(port, rpcRequest, channelRegList, channelData);
            for (const auto& item: channelRegList) {
                if (item.Register->GetConfig()->AccessType == TRegisterConfig::EAccessType::READ_ONLY) {
                    readonlyList.append(item.Id);
                }
            }
            if (!channelData.empty()) {
                result["channels"] = channelData;
            }

            // Step 3: Read explicitly requested parameters, reusing values already
            // read for condition evaluation to avoid duplicate Modbus reads
            Json::Value paramData(Json::objectValue);
            auto paramRegList = rpcRequest->GetParametersRegisterList(conditionParamValues, &paramData);
            ReadRegisters(port, rpcRequest, paramRegList, paramData);
            if (!paramData.empty()) {
                result["parameters"] = paramData;
            }

            if (!readonlyList.empty()) {
                result["readonly"] = readonlyList;
            }
        } catch (...) {
            EndSession(*port, rpcRequest->Device);
            throw;
        }
        EndSession(*port, rpcRequest->Device);

        rpcRequest->OnResult(result);
    }
} // namespace

TRPCDeviceLoadRequest::TRPCDeviceLoadRequest(const TDeviceProtocolParams& protocolParams,
                                             PSerialDevice device,
                                             PDeviceTemplate deviceTemplate,
                                             bool deviceFromConfig)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig)
{}

void TRPCDeviceLoadRequest::ParseRequestItems(const Json::Value& items, std::set<std::string>& list)
{
    for (const auto& item: items) {
        list.insert(item.asString());
    }
}

TRPCRegisterList TRPCDeviceLoadRequest::GetConditionParametersRegisterList()
{
    // Collect the parameter names referenced by conditions of the requested channels and parameters,
    // an empty channel list requests all channels
    std::set<std::string> neededParams;
    Expressions::TExpressionsCache exprCache;
    auto allChannels = Channels.empty();
    for (const auto& ch: DeviceTemplate->GetTemplate()["channels"]) {
        if (ch["address"].isNull()) { // write only channel
            continue;
        }
        if (!allChannels && !Channels.count(ch["name"].asString())) {
            continue;
        }
        auto deps = Expressions::GetDependencies(ch["condition"].asString(), exprCache);
        neededParams.insert(deps.begin(), deps.end());
    }
    const auto& params = DeviceTemplate->GetTemplate()["parameters"];
    for (auto it = params.begin(); it != params.end(); ++it) {
        const auto& item = *it;
        if (item["address"].isNull()) {
            continue;
        }
        auto id = params.isObject() ? it.key().asString() : item["id"].asString();
        if (!Parameters.count(id)) {
            continue;
        }
        auto deps = Expressions::GetDependencies(item["condition"].asString(), exprCache);
        neededParams.insert(deps.begin(), deps.end());
    }
    if (neededParams.empty()) {
        return {};
    }
    return CreateParametersRegisterList(ProtocolParams, Device, params, Json::Value(), neededParams);
}

TRPCRegisterList TRPCDeviceLoadRequest::GetChannelsRegisterList(const Json::Value& conditionParams)
{
    auto notFound = Channels;
    auto allChannels = Channels.empty();
    Json::Value items(Json::arrayValue);
    for (const auto& item: DeviceTemplate->GetTemplate()["channels"]) {
        if (item["address"].isNull()) { // write only channel
            continue;
        }
        auto id = item["name"].asString();
        if (allChannels || Channels.count(id)) {
            auto channel = item;
            channel["id"] = id;
            items.append(channel);
            notFound.erase(id);
        }
    }
    if (!notFound.empty()) {
        throw TRPCException("Channel \"" + *notFound.begin() + "\" is write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    // Filter channels by condition using actual device parameter values,
    // conditions over parameters without a value are evaluated with an undefined value
    if (!conditionParams.isNull()) {
        Expressions::TExpressionsCache cache;
        TJsonParams exprParams(conditionParams);
        Json::Value filtered(Json::arrayValue);
        for (const auto& item: items) {
            if (CheckCondition(item, exprParams, &cache)) {
                filtered.append(item);
            }
        }
        items = filtered;
    }

    return CreateChannelsRegisterList(ProtocolParams, Device, items);
}

TRPCRegisterList TRPCDeviceLoadRequest::GetParametersRegisterList(const Json::Value& conditionParams, Json::Value* data)
{
    const auto& params = DeviceTemplate->GetTemplate()["parameters"];
    auto notFound = Parameters;
    Expressions::TExpressionsCache cache;
    TJsonParams exprParams(conditionParams);
    TActiveParameterDeclarations matched;
    Json::Value items(Json::arrayValue);
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto item = *it;
        if (item["address"].isNull()) {
            continue;
        }
        auto id = params.isObject() ? it.key().asString() : item["id"].asString();
        if (!Parameters.count(id)) {
            continue;
        }
        notFound.erase(id);
        if (!conditionParams.isNull()) {
            // conditions over parameters without a value are evaluated with an undefined value
            if (!CheckCondition(item, exprParams, &cache)) {
                continue;
            }
            // A chain of fw variants is not ambiguous, CreateParametersRegisterList merges it into one register.
            // Anything else is a template error, config validation reports it as a duplicate definition
            if (!matched.Add(id, *it)) {
                throw TRPCException("Parameter \"" + id +
                                        "\" is ambiguous: several declarations match the condition parameter values",
                                    TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
        if (conditionParams.isMember(id)) {
            // already read for condition evaluation, declarations of such a parameter read identically
            if (data != nullptr) {
                (*data)[id] = conditionParams[id];
            }
            continue;
        }
        if (params.isObject()) {
            item["id"] = id;
        }
        items.append(item);
    }
    if (!notFound.empty()) {
        throw TRPCException("Parameter \"" + *notFound.begin() + "\" is write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    return CreateParametersRegisterList(ProtocolParams, Device, items);
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
