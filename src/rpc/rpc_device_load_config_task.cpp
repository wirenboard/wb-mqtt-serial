#include "rpc_device_load_config_task.h"
#include "config_merge_template.h"
#include "port/serial_port.h"
#include "rpc_helpers.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const auto MAX_RETRIES = 2;

    bool ReadModbusRegister(Modbus::IModbusTraits& traits,
                            TPort& port,
                            PRPCDeviceLoadConfigRequest rpcRequest,
                            PRegisterConfig registerConfig,
                            TRegisterValue& value)
    {
        uint8_t slaveId = static_cast<uint8_t>(std::stoi(rpcRequest->Device->DeviceConfig()->SlaveId));
        for (int i = 0; i <= MAX_RETRIES; ++i) {
            try {
                value = Modbus::ReadRegister(traits,
                                             port,
                                             slaveId,
                                             *registerConfig,
                                             std::chrono::microseconds(0),
                                             rpcRequest->ResponseTimeout,
                                             rpcRequest->FrameTimeout);
                return true;
            } catch (const Modbus::TModbusExceptionError& err) {
                if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
                    err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
                {
                    break;
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

        return false;
    }

    std::string ReadWbRegister(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest, const std::string& registerName)
    {
        std::string error;
        try {
            Modbus::TModbusRTUTraits traits;
            auto config = WbRegisters::GetRegisterConfig(registerName);
            TRegisterValue value;
            std::string result;
            if (ReadModbusRegister(traits, port, rpcRequest, config, value)) {
                result = value.Get<std::string>();
            }
            return result;
        } catch (const Modbus::TErrorBase& err) {
            error = err.what();
        } catch (const TResponseTimeoutException& e) {
            error = e.what();
        }
        LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->Device->DeviceConfig()->SlaveId
                  << " unable to read \"" << registerName << "\" register: " << error;
        throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    std::string ReadDeviceModel(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest)
    {
        auto modelName = ReadWbRegister(port, rpcRequest, WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME);
        if (modelName.empty()) {
            modelName = ReadWbRegister(port, rpcRequest, WbRegisters::DEVICE_MODEL_REGISTER_NAME);
        }
        return modelName;
    }

    std::string ReadFwVersion(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest)
    {
        return ReadWbRegister(port, rpcRequest, WbRegisters::FW_VERSION_REGISTER_NAME);
    }

    void ExecRPCRequest(PPort port, PRPCDeviceLoadConfigRequest rpcRequest)
    {
        if (!rpcRequest->OnResult) {
            return;
        }

        Json::Value templateParams = rpcRequest->DeviceTemplate->GetTemplate()["parameters"];
        if (templateParams.empty()) {
            rpcRequest->OnResult(Json::Value(Json::objectValue));
            return;
        }

        std::string id = rpcRequest->ParametersCache.GetId(*port, rpcRequest->Device->DeviceConfig()->SlaveId);
        std::string deviceModel;
        std::string fwVersion;
        Json::Value parameters;
        if (rpcRequest->ParametersCache.Contains(id)) {
            Json::Value cache = rpcRequest->ParametersCache.Get(id);
            deviceModel = cache["model"].asString();
            fwVersion = cache["fw"].asString();
            parameters = cache["parameters"];
        }
        if (parameters.isNull()) {
            for (const auto& item: rpcRequest->Device->GetSetupItems()) {
                if (!item->ParameterId.empty()) {
                    parameters[item->ParameterId] = RawValueToJSON(*item->RegisterConfig, item->RawValue);
                }
            }
        }

        port->SkipNoise();

        if (rpcRequest->IsWBDevice) {
            if (deviceModel.empty()) {
                deviceModel = ReadDeviceModel(*port, rpcRequest);
            }
            if (fwVersion.empty()) {
                fwVersion = ReadFwVersion(*port, rpcRequest);
            }
            if (deviceModel.empty() || fwVersion.empty()) {
                throw TRPCException("Unable to read device model or firmware version",
                                    TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
            auto match = false;
            for (const auto& item: rpcRequest->DeviceTemplate->GetHardware()) {
                if (item.Signature == deviceModel) {
                    match = util::CompareVersionStrings(item.Fw, fwVersion) <= 0;
                    break;
                }
            }
            if (!match) {
                throw TRPCException("Device model or firmware version is not compatible with selected template",
                                    TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }

        std::list<std::string> paramsList;
        auto registerList = CreateRegisterList(
            rpcRequest->ProtocolParams,
            rpcRequest->Device,
            rpcRequest->Group.empty() ? templateParams
                                      : GetTemplateParamsGroup(templateParams, rpcRequest->Group, paramsList),
            parameters,
            fwVersion);
        ReadRegisterList(*port, rpcRequest->Device, registerList, parameters, MAX_RETRIES);

        Json::Value result;
        if (!deviceModel.empty()) {
            result["model"] = deviceModel;
        }
        if (!fwVersion.empty()) {
            result["fw"] = fwVersion;
        }
        if (!paramsList.empty()) {
            for (const auto& id: paramsList) {
                result["parameters"][id] = parameters[id];
            }
        } else {
            result["parameters"] = parameters;
        }
        CheckParametersConditions(templateParams, result["parameters"]);
        rpcRequest->OnResult(result);

        if (rpcRequest->DeviceFromConfig) {
            result["parameters"] = parameters;
            rpcRequest->ParametersCache.Add(id, result);
        }
    }
} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const TDeviceProtocolParams& protocolParams,
                                                         PSerialDevice device,
                                                         PDeviceTemplate deviceTemplate,
                                                         bool deviceFromConfig,
                                                         TRPCDeviceParametersCache& parametersCache)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig),
      ParametersCache(parametersCache)
{
    IsWBDevice =
        !DeviceTemplate->GetHardware().empty() || DeviceTemplate->GetTemplate()["enable_wb_continuous_read"].asBool();
}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TDeviceProtocolParams& protocolParams,
                                                            PSerialDevice device,
                                                            PDeviceTemplate deviceTemplate,
                                                            bool deviceFromConfig,
                                                            TRPCDeviceParametersCache& parametersCache,
                                                            WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                            WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto res = std::make_shared<TRPCDeviceLoadConfigRequest>(protocolParams,
                                                             device,
                                                             deviceTemplate,
                                                             deviceFromConfig,
                                                             parametersCache);
    res->ParseSettings(request, onResult, onError);
    res->Group = request["group"].asString();
    return res;
}

TRPCDeviceLoadConfigSerialClientTask::TRPCDeviceLoadConfigSerialClientTask(PRPCDeviceLoadConfigRequest request)
    : Request(request)
{
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCDeviceLoadConfigSerialClientTask::Run(
    PFeaturePort port,
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
        }
        ExecRPCRequest(port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}

Json::Value GetTemplateParamsGroup(const Json::Value& templateParams,
                                   const std::string& group,
                                   std::list<std::string>& paramsList)
{
    Json::Value result;
    std::list<std::string> conditionList;
    bool check = true;
    while (check) {
        check = false;
        for (auto it = templateParams.begin(); it != templateParams.end(); ++it) {
            const Json::Value& data = *it;
            std::string id = templateParams.isObject() ? it.key().asString() : data["id"].asString();
            if (std::find(conditionList.begin(), conditionList.end(), id) == conditionList.end() &&
                data["group"].asString() != group)
            {
                continue;
            }
            if (std::find(paramsList.begin(), paramsList.end(), id) == paramsList.end()) {
                paramsList.push_back(id);
                result[id] = data;
            }
            if (data["condition"].isNull()) {
                continue;
            }
            Expressions::TLexer lexer;
            auto tokens = lexer.GetTokens(data["condition"].asString());
            for (const auto& token: tokens) {
                if (token.Type == Expressions::TTokenType::Ident && token.Value != "isDefined" &&
                    std::find(conditionList.begin(), conditionList.end(), token.Value) == conditionList.end())
                {
                    conditionList.push_back(token.Value);
                    check = true;
                }
            }
        }
    }
    return result;
}

void CheckParametersConditions(const Json::Value& templateParams, Json::Value& parameters)
{
    TJsonParams jsonParams(parameters);
    Expressions::TExpressionsCache expressionsCache;
    bool check = true;
    while (check) {
        std::unordered_map<std::string, bool> matches;
        for (auto it = templateParams.begin(); it != templateParams.end(); ++it) {
            const Json::Value& registerData = *it;
            std::string id = templateParams.isObject() ? it.key().asString() : registerData["id"].asString();
            if (!parameters.isMember(id)) {
                continue;
            }
            bool match = CheckCondition(registerData, jsonParams, &expressionsCache);
            if (matches.find(id) == matches.end() || match) {
                matches[id] = match;
            }
        }
        check = false;
        for (auto it = matches.begin(); it != matches.end(); ++it) {
            if (!it->second) {
                parameters.removeMember(it->first);
                check = true;
            }
        }
    }
    if (parameters.isNull()) {
        parameters = Json::Value(Json::objectValue);
    }
}

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val)
{
    switch (reg.Format) {
        case U8:
            return val.Get<uint8_t>();
        case S8:
            return val.Get<int8_t>();
        case S16:
            return val.Get<int16_t>();
        case S24: {
            uint32_t v = val.Get<uint64_t>() & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return static_cast<int32_t>(v);
        }
        case S32:
            return val.Get<int32_t>();
        case S64:
            return val.Get<int64_t>();
        case BCD8:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W8_SZ);
        case BCD16:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W16_SZ);
        case BCD24:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W24_SZ);
        case BCD32:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W32_SZ);
        case Float: {
            float v;
            auto rawValue = val.Get<uint64_t>();

            // codacy static code analysis fails on this memcpy call, have no idea how to fix it
            memcpy(&v, &rawValue, sizeof(v));

            return v;
        }
        case Double: {
            double v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return v;
        }
        case Char8:
            return std::string(1, val.Get<uint8_t>());
        case String:
        case String8:
            return val.Get<std::string>();
        default:
            return val.Get<uint64_t>();
    }
}
