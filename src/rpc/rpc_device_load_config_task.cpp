#include "rpc_device_load_config_task.h"
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
                            PRPCDeviceLoadConfigRequest rpcRequest,
                            PRegisterConfig registerConfig,
                            TRegisterValue& value)
    {
        auto slaveId = static_cast<uint8_t>(std::stoi(rpcRequest->Device->DeviceConfig()->SlaveId));
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (rpcRequest->ProtocolParams.protocol->GetName() == "modbus-tcp") {
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
                                             rpcRequest->ResponseTimeout,
                                             rpcRequest->FrameTimeout);
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
                             PRPCDeviceLoadConfigRequest rpcRequest,
                             PRegisterConfig registerConfig,
                             const TRegisterValue& value)
    {
        auto slaveId = static_cast<uint8_t>(std::stoi(rpcRequest->Device->DeviceConfig()->SlaveId));
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (rpcRequest->ProtocolParams.protocol->GetName() == "modbus-tcp") {
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
                                      rpcRequest->ResponseTimeout,
                                      rpcRequest->FrameTimeout);
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

    std::string ReadWbRegister(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest, const std::string& registerName)
    {
        std::string error;
        try {
            auto config = WbRegisters::GetRegisterConfig(registerName);
            TRegisterValue value;
            ReadModbusRegister(port, rpcRequest, config, value);
            return value.Get<std::string>();
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
        try {
            return ReadWbRegister(port, rpcRequest, WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME);
        } catch (const Modbus::TErrorBase& err) {
            return ReadWbRegister(port, rpcRequest, WbRegisters::DEVICE_MODEL_REGISTER_NAME);
        }
    }

    void SetContinuousRead(TPort& port, PRPCDeviceLoadConfigRequest rpcRequest, bool enabled)
    {
        std::string error;
        try {
            auto config = WbRegisters::GetRegisterConfig(WbRegisters::CONTINUOUS_READ_REGISTER_NAME);
            WriteModbusRegister(port, rpcRequest, config, TRegisterValue(enabled));
        } catch (const Modbus::TErrorBase& err) {
            error = err.what();
        } catch (const TResponseTimeoutException& e) {
            error = e.what();
        }
        if (!error.empty()) {
            LOG(Warn) << port.GetDescription() << " modbus:" << rpcRequest->Device->DeviceConfig()->SlaveId
                      << " unable to write \"" << WbRegisters::CONTINUOUS_READ_REGISTER_NAME
                      << "\" register: " << error;
            throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }
    }

    void LoadConfigParameters(PPort port, PRPCDeviceLoadConfigRequest rpcRequest, Json::Value& parameters)
    {
        Json::Value config(WBMQTT::JSON::Parse(rpcRequest->ConfigFileName));
        for (const auto& port: config["ports"]) {
            for (auto cfg: port["devices"]) {
                const auto& dev = rpcRequest->Device->DeviceConfig();
                if (cfg["device_type"].asString() == dev->DeviceType && cfg["slave_id"].asString() == dev->SlaveId) {
                    cfg.removeMember("channels");
                    cfg.removeMember("device_type");
                    cfg.removeMember("slave_id");
                    parameters = cfg;
                    return;
                }
            }
        }
    }

    void CheckTemplate(PPort port, PRPCDeviceLoadConfigRequest rpcRequest, std::string& model)
    {
        const auto& version = rpcRequest->Device->GetWbFwVersion();
        if (model.empty()) {
            model = ReadDeviceModel(*port, rpcRequest);
        }
        for (const auto& item: rpcRequest->DeviceTemplate->GetHardware()) {
            if (item.Signature == model) {
                if (util::CompareVersionStrings(version, item.Fw) >= 0) {
                    return;
                }
                throw TRPCException("Device \"" + model + "\" firmware version " + version +
                                        " is lower than selected template minimal supported version " + item.Fw,
                                    TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
        throw TRPCException("Device \"" + model + "\" with firmware version " + version +
                                " is incompatible with selected template device models",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    void MarkUnsupportedParameters(TPort& port,
                                   PRPCDeviceLoadConfigRequest rpcRequest,
                                   TRPCRegisterList& registerList,
                                   Json::Value& parameters)
    {
        auto continuousRead = true;
        for (auto it = registerList.begin(); it != registerList.end(); ++it) {
            const auto& item = *it;
            try {
                // this code checks registers only for 16-bit register unsupported value 0xFFFE
                // it must be modified to check larger registers like 24, 32 or 64-bits
                if (item.CheckUnsupported && item.Register->GetValue().Get<uint16_t>() == 0xFFFE) {
                    if (continuousRead) {
                        SetContinuousRead(port, rpcRequest, false);
                        continuousRead = false;
                    }
                    try {
                        TRegisterValue value;
                        ReadModbusRegister(port, rpcRequest, item.Register->GetConfig(), value);
                    } catch (const Modbus::TModbusExceptionError& err) {
                        parameters[item.Id] = UNSUPPORTED_VALUE;
                    }
                }
            } catch (const TRegisterValueException& e) {
            }
        }
        if (!continuousRead && rpcRequest->DeviceFromConfig) {
            SetContinuousRead(port, rpcRequest, true);
        }
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
        Json::Value parameters;
        if (rpcRequest->ParametersCache.Contains(id)) {
            Json::Value cache = rpcRequest->ParametersCache.Get(id);
            deviceModel = cache["model"].asString();
            parameters = cache["parameters"];
        }
        if (parameters.isNull()) {
            LoadConfigParameters(port, rpcRequest, parameters);
        }

        port->SkipNoise();
        PrepareSession(*port, rpcRequest->Device, MAX_RETRIES);

        if (rpcRequest->Device->IsWbDevice()) {
            CheckTemplate(port, rpcRequest, deviceModel);
        }

        std::list<std::string> paramsList;
        auto registerList = CreateRegisterList(rpcRequest->ProtocolParams,
                                               rpcRequest->Device,
                                               templateParams,
                                               parameters,
                                               rpcRequest->Device->GetWbFwVersion(),
                                               rpcRequest->Device->IsWbDevice());
        ReadRegisterList(*port, rpcRequest->Device, registerList, MAX_RETRIES);
        GetRegisterListParameters(registerList, parameters);
        MarkUnsupportedParameters(*port, rpcRequest, registerList, parameters);

        Json::Value result(Json::objectValue);
        result["parameters"] = parameters;

        if (!deviceModel.empty()) {
            result["model"] = deviceModel;
        }
        if (!rpcRequest->Device->GetWbFwVersion().empty()) {
            result["fw"] = rpcRequest->Device->GetWbFwVersion();
        }
        if (rpcRequest->DeviceFromConfig) {
            rpcRequest->ParametersCache.Add(id, result);
        }

        rpcRequest->OnResult(result);
    }
} // namespace

TRPCDeviceLoadConfigRequest::TRPCDeviceLoadConfigRequest(const TDeviceProtocolParams& protocolParams,
                                                         PSerialDevice device,
                                                         PDeviceTemplate deviceTemplate,
                                                         bool deviceFromConfig,
                                                         const std::string& configFileName,
                                                         TRPCDeviceParametersCache& parametersCache)
    : TRPCDeviceRequest(protocolParams, device, deviceTemplate, deviceFromConfig),
      ConfigFileName(configFileName),
      ParametersCache(parametersCache)
{}

PRPCDeviceLoadConfigRequest ParseRPCDeviceLoadConfigRequest(const Json::Value& request,
                                                            const TDeviceProtocolParams& protocolParams,
                                                            PSerialDevice device,
                                                            PDeviceTemplate deviceTemplate,
                                                            bool deviceFromConfig,
                                                            const std::string& configFileName,
                                                            TRPCDeviceParametersCache& parametersCache,
                                                            WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                            WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    auto res = std::make_shared<TRPCDeviceLoadConfigRequest>(protocolParams,
                                                             device,
                                                             deviceTemplate,
                                                             deviceFromConfig,
                                                             configFileName,
                                                             parametersCache);
    res->ParseSettings(request, onResult, onError);
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

void GetRegisterListParameters(const TRPCRegisterList& registerList, Json::Value& parameters)
{
    TJsonParams jsonParams(parameters);
    Expressions::TExpressionsCache expressionsCache;
    bool check = true;
    while (check) {
        check = false;
        for (const auto& item: registerList) {
            if (item.Register->GetValue().GetType() == TRegisterValue::ValueType::Undefined) {
                continue;
            }
            if (!parameters.isMember(item.Id) && CheckCondition(item.Condition, jsonParams, &expressionsCache)) {
                parameters[item.Id] = item.Register->IsSupported()
                                          ? RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue())
                                          : UNSUPPORTED_VALUE;
                check = true;
            }
        }
    }
}
