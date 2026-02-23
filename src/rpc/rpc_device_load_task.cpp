#include "rpc_device_load_task.h"
#include "config_merge_template.h"
#include "port/serial_port.h"
#include "rpc_helpers.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const auto MAX_RETRIES = 2;
    const auto UNSUPPORTED_VALUE = "unsupported";

    void ReadModbusRegister(TPort& port,
                            TRPCDeviceRequest& request,
                            PRegisterConfig registerConfig,
                            TRegisterValue& value)
    {
        auto slaveId = static_cast<uint8_t>(std::stoi(request.Device->DeviceConfig()->SlaveId));
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (request.ProtocolParams.protocol->GetName() == "modbus-tcp") {
            traits = std::make_unique<Modbus::TModbusTCPTraits>();
        } else {
            traits = std::make_unique<Modbus::TModbusRTUTraits>();
        }
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                value = Modbus::ReadRegister(*traits,
                                             port,
                                             slaveId,
                                             *registerConfig,
                                             std::chrono::microseconds(0),
                                             request.ResponseTimeout,
                                             request.FrameTimeout);
                return;
            } catch (const Modbus::TModbusExceptionError& err) {
                if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                {
                    throw;
                }
            } catch (const Modbus::TErrorBase& err) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            } catch (const TResponseTimeoutException& e) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            }
        }
    }

    void WriteModbusRegister(TPort& port,
                             TRPCDeviceRequest& request,
                             PRegisterConfig registerConfig,
                             const TRegisterValue& value)
    {
        auto slaveId = static_cast<uint8_t>(std::stoi(request.Device->DeviceConfig()->SlaveId));
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (request.ProtocolParams.protocol->GetName() == "modbus-tcp") {
            traits = std::make_unique<Modbus::TModbusTCPTraits>();
        } else {
            traits = std::make_unique<Modbus::TModbusRTUTraits>();
        }
        Modbus::TRegisterCache cache;
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                Modbus::WriteRegister(*traits,
                                      port,
                                      slaveId,
                                      *registerConfig,
                                      value,
                                      cache,
                                      std::chrono::microseconds(0),
                                      request.ResponseTimeout,
                                      request.FrameTimeout);
                return;
            } catch (const Modbus::TModbusExceptionError& err) {
                if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                {
                    throw;
                }
            } catch (const Modbus::TErrorBase& err) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            } catch (const TResponseTimeoutException& e) {
                if (i == MAX_RETRIES) {
                    throw;
                }
            }
        }
    }

    void SetContinuousRead(TPort& port, TRPCDeviceRequest& request, bool enabled)
    {
        std::string error;
        try {
            auto config = WbRegisters::GetRegisterConfig(WbRegisters::CONTINUOUS_READ_REGISTER_NAME);
            WriteModbusRegister(port, request, config, TRegisterValue(enabled));
        } catch (const Modbus::TErrorBase& err) {
            error = err.what();
        } catch (const TResponseTimeoutException& e) {
            error = e.what();
        }
        if (!error.empty()) {
            LOG(Warn) << port.GetDescription() << " modbus:" << request.Device->DeviceConfig()->SlaveId
                      << " unable to write \"" << WbRegisters::CONTINUOUS_READ_REGISTER_NAME
                      << "\" register: " << error;
        }
    }

    bool IsAllFFFE(const TRegisterValue& value)
    {
        if (value.GetType() == TRegisterValue::ValueType::Integer) {
            return value.Get<uint16_t>() == 0xFFFE;
        }
        if (value.GetType() == TRegisterValue::ValueType::String) {
            // String format extracts lower byte of each 16-bit register word.
            // All-0xFFFE registers produce a string of all '\xFE' bytes.
            // String8 format extracts both bytes, producing '\xFF\xFE' pairs.
            // An empty string means GetStringRegisterValue hit '\xFF' at the
            // first character — which also indicates unsupported.
            const auto& s = value.Get<std::string>();
            if (s.empty()) {
                return true;
            }
            for (auto ch: s) {
                if (static_cast<uint8_t>(ch) != 0xFE) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    void MarkUnsupported(TPort& port,
                         TRPCDeviceRequest& request,
                         TRPCRegisterList& registerList,
                         Json::Value& data)
    {
        auto continuousRead = true;
        for (const auto& item: registerList) {
            try {
                if (item.CheckUnsupported && IsAllFFFE(item.Register->GetValue())) {
                    if (continuousRead) {
                        SetContinuousRead(port, request, false);
                        continuousRead = false;
                    }
                    try {
                        TRegisterValue value;
                        ReadModbusRegister(port, request, item.Register->GetConfig(), value);
                    } catch (const Modbus::TModbusExceptionError& err) {
                        data[item.Id] = UNSUPPORTED_VALUE;
                    }
                }
            } catch (const TRegisterValueException& e) {
            }
        }
        if (!continuousRead && request.DeviceFromConfig) {
            SetContinuousRead(port, request, true);
        }
    }

    void ReadRegistersIntoJson(PPort port,
                               PRPCDeviceLoadRequest rpcRequest,
                               TRPCRegisterList& registerList,
                               Json::Value& data,
                               Json::Value& readonlyMap)
    {
        ReadRegisterList(*port, rpcRequest->Device, registerList);
        for (const auto& item: registerList) {
            data[item.Id] = item.Register->IsSupported()
                                ? RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue())
                                : UNSUPPORTED_VALUE;
            if (item.Register->GetConfig()->AccessType == TRegisterConfig::EAccessType::READ_ONLY) {
                readonlyMap[item.Id] = true;
            }
        }
        MarkUnsupported(*port, *rpcRequest, registerList, data);
    }

    void ExecRPCRequest(PPort port, PRPCDeviceLoadRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        PrepareSession(*port, rpcRequest->Device);

        Json::Value result(Json::objectValue);
        Json::Value readonlyMap(Json::objectValue);

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
        auto channelRegList = rpcRequest->GetChannelsRegisterList(conditionParamValues);
        if (!channelRegList.empty()) {
            Json::Value channelData(Json::objectValue);
            ReadRegistersIntoJson(port, rpcRequest, channelRegList, channelData, readonlyMap);
            result["channels"] = channelData;
        }

        // Step 3: Read explicitly requested parameters
        auto paramRegList = rpcRequest->GetParametersRegisterList();
        if (!paramRegList.empty()) {
            Json::Value paramData(Json::objectValue);
            ReadRegistersIntoJson(port, rpcRequest, paramRegList, paramData, readonlyMap);
            result["parameters"] = paramData;
        }

        if (!readonlyMap.empty()) {
            result["readonly"] = readonlyMap;
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
        if (item["address"].isNull()) {
            continue;
        }
        if (item.isMember("enabled") && !item["enabled"].asBool()) {
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
    return CreateRegisterList(ProtocolParams, Device, items, Json::Value(), Device ? Device->GetWbFwVersion() : std::string(), Device && Device->IsWbDevice());
}

TRPCRegisterList TRPCDeviceLoadRequest::GetChannelsRegisterList(const Json::Value& conditionParams)
{
    Json::Value items(Json::arrayValue);
    const bool allChannels = Channels.empty();
    for (auto item: DeviceTemplate->GetTemplate()["channels"]) {
        if (item["address"].isNull()) { // write only channel
            continue;
        }
        auto id = item["name"].asString();
        if (allChannels) {
            item["id"] = id;
            items.append(item);
        } else if (std::find(Channels.begin(), Channels.end(), id) != Channels.end()) {
            item["id"] = id;
            items.append(item);
            Channels.remove(id);
        }
    }
    if (!allChannels && !Channels.empty()) {
        throw TRPCException("Channel \"" + Channels.front() + "\" is write only or not found in \"" +
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

    return CreateRegisterList(ProtocolParams, Device, items, Json::Value(), Device ? Device->GetWbFwVersion() : std::string(), Device && Device->IsWbDevice());
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
        if (item.isMember("enabled") && !item["enabled"].asBool()) {
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
        throw TRPCException("Parameter \"" + Parameters.front() + "\" is write only or not found in \"" +
                                DeviceTemplate->Type + "\" device template",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return CreateRegisterList(ProtocolParams, Device, items, Json::Value(), Device ? Device->GetWbFwVersion() : std::string(), Device && Device->IsWbDevice());
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
