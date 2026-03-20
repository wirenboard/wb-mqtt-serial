#include "rpc_device_load_task.h"
#include "config_merge_template.h"
#include "port/serial_port.h"
#include "rpc_helpers.h"

namespace
{
    void ReadRegisters(PPort port,
                       PRPCDeviceLoadRequest rpcRequest,
                       TRPCRegisterList& registerList,
                       Json::Value& data,
                       Json::Value& readonlyList)
    {
        ReadRegisterList(*port, rpcRequest->Device, registerList);
        for (const auto& item: registerList) {
            data[item.Id] = item.Register->IsSupported()
                                ? RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue())
                                : UNSUPPORTED_VALUE;
            if (item.Register->GetConfig()->AccessType == TRegisterConfig::EAccessType::READ_ONLY) {
                readonlyList.append(item.Id);
            }
        }
        MarkUnsupportedRegisterItems(*port, *rpcRequest, registerList, data);
    }

    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        PrepareSession(*port, rpcRequest->Device);

        // Step 1: Read parameters that are referenced by channel conditions
        Json::Value conditionParamValues(Json::objectValue);
        auto condParamRegList = rpcRequest->GetConditionParametersRegisterList();
        if (!condParamRegList.empty()) {
            ReadRegisterList(*port, rpcRequest->Device, condParamRegList);
            for (const auto& item: condParamRegList) {
                if (item.Register->IsSupported()) {
                    conditionParamValues[item.Id] =
                        RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue());
                }
            }
        }

        // Step 2: Read channels, filtering by condition using actual parameter values
        Json::Value readonlyList(Json::arrayValue);
        Json::Value result(Json::objectValue);
        auto channelRegList = rpcRequest->GetChannelsRegisterList(conditionParamValues);
        if (!channelRegList.empty()) {
            Json::Value channelData(Json::objectValue);
            ReadRegisters(port, rpcRequest, channelRegList, channelData, readonlyList);
            result["channels"] = channelData;
        }

        // Step 3: Read explicitly requested parameters, reusing values already
        // read for condition evaluation to avoid duplicate Modbus reads
        Json::Value paramData(Json::objectValue);
        for (const auto& id: rpcRequest->Parameters) {
            if (conditionParamValues.isMember(id)) {
                paramData[id] = conditionParamValues[id];
            }
        }
        auto paramRegList = rpcRequest->GetParametersRegisterList(conditionParamValues);
        if (!paramRegList.empty()) {
            ReadRegisters(port, rpcRequest, paramRegList, paramData, readonlyList);
            result["parameters"] = paramData;
        }

        if (!readonlyList.empty()) {
            result["readonly"] = readonlyList;
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

TRPCRegisterList TRPCDeviceLoadRequest::GetConditionParametersRegisterList()
{
    auto channels = DeviceTemplate->GetTemplate()["channels"];
    auto params = DeviceTemplate->GetTemplate()["parameters"];

    // Collect all parameter names referenced in channel conditions
    std::set<std::string> neededParams;
    Expressions::TExpressionsCache exprCache;
    Expressions::TParser parser;
    for (const auto& ch: channels) {
        if (!ch.isMember("condition") || ch["condition"].asString().empty()) {
            continue;
        }
        auto cond = ch["condition"].asString();
        auto itExpr = exprCache.find(cond);
        if (itExpr == exprCache.end()) {
            itExpr = exprCache.emplace(cond, parser.Parse(cond)).first;
        }
        auto deps = Expressions::GetDependencies(itExpr->second.get());
        neededParams.insert(deps.begin(), deps.end());
    }

    if (neededParams.empty()) {
        return {};
    }

    // Build register list for the needed parameters
    Json::Value items(Json::arrayValue);
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto item = *it;
        if (item["address"].isNull() || !item.get("enabled", true).asBool()) {
            continue;
        }
        auto id = params.isObject() ? it.key().asString() : item["id"].asString();
        if (neededParams.count(id)) {
            if (params.isObject()) {
                item["id"] = id;
            }
            items.append(item);
        }
    }

    // nullptr checks is needed for tests
    return CreateRegisterList(ProtocolParams,
                              Device,
                              items,
                              Json::Value(),
                              Device ? Device->GetWbFwVersion() : std::string(),
                              Device && Device->IsWbDevice());
}

TRPCRegisterList TRPCDeviceLoadRequest::GetChannelsRegisterList(const Json::Value& conditionParams)
{
    auto notFound = Channels;
    auto allChannels = Channels.empty();
    Json::Value items(Json::arrayValue);
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) {
        if (item["address"].isNull()) { // write only channel
            continue;
        }
        auto id = item["name"].asString();
        if (allChannels || std::find(Channels.begin(), Channels.end(), id) != Channels.end()) {
            item["id"] = id;
            items.append(item);
            notFound.remove(id);
        }
    }
    if (!notFound.empty()) {
        throw TRPCException("Channel \"" + notFound.front() + "\" is disabled, write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    // Filter channels by condition using actual device parameter values
    if (!conditionParams.empty()) {
        TJsonParams jsonParams(conditionParams);
        Expressions::TExpressionsCache cache;
        Json::Value filtered(Json::arrayValue);
        for (const auto& item: items) {
            if (CheckCondition(item, jsonParams, &cache)) {
                filtered.append(item);
            }
        }
        items = filtered;
    }

    // nullptr checks is needed for tests
    return CreateRegisterList(ProtocolParams,
                              Device,
                              items,
                              Json::Value(),
                              Device ? Device->GetWbFwVersion() : std::string(),
                              Device && Device->IsWbDevice());
}

TRPCRegisterList TRPCDeviceLoadRequest::GetParametersRegisterList(const Json::Value& knownValues)
{
    auto params = DeviceTemplate->GetTemplate()["parameters"];
    Json::Value items(Json::arrayValue);
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto item = *it;
        if (item["address"].isNull() || !item.get("enabled", true).asBool()) {
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
    if (!Parameters.empty()) {
        throw TRPCException("Parameter \"" + Parameters.front() + "\" is disabled, write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    // nullptr checks is needed for tests
    return CreateRegisterList(ProtocolParams,
                              Device,
                              items,
                              knownValues,
                              Device ? Device->GetWbFwVersion() : std::string(),
                              Device && Device->IsWbDevice());
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
