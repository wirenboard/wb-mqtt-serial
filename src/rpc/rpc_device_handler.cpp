#include "rpc_device_handler.h"
#include "rpc_device_load_config_task.h"
#include "rpc_device_load_task.h"
#include "rpc_device_probe_task.h"
#include "rpc_device_set_task.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    bool IsSupportedByFw(const Json::Value& item, const std::string& fwVersion)
    {
        if (fwVersion.empty()) {
            return true;
        }
        std::string fw = item["fw"].asString();
        return fw.empty() || util::CompareVersionStrings(fw, fwVersion) <= 0;
    }

    TRPCRegister CreateRPCRegister(const TDeviceProtocolParams& protocolParams,
                                   PSerialDevice device,
                                   const std::string& id,
                                   const Json::Value& item,
                                   bool checkUnsupported)
    {
        auto config = LoadRegisterConfig(item,
                                         *protocolParams.protocol->GetRegTypes(),
                                         std::string(),
                                         *protocolParams.factory,
                                         protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress(),
                                         0);
        TRPCRegister reg = {id,
                            item["condition"].asString(),
                            std::make_shared<TRegister>(device, config.RegisterConfig),
                            checkUnsupported};
        reg.Register->SetAvailable(TRegisterAvailability::AVAILABLE);

        // this code checks enums and ranges only for 16-bit register unsupported value 0xFFFE
        // it must be modified to check larger registers like 24, 32 or 64-bits
        if (reg.CheckUnsupported) {
            int unsupportedValue =
                config.RegisterConfig->Format == S16 ? static_cast<int16_t>(0xFFFE) : static_cast<uint16_t>(0xFFFE);
            if (item.isMember("enum")) {
                for (const auto& value: item["enum"]) {
                    try {
                        if (std::stoi(value.asString(), 0, 0) == unsupportedValue) {
                            reg.CheckUnsupported = false;
                            break;
                        }
                    } catch (const std::logic_error&) {
                    }
                }
            } else {
                if (item["min"].asInt() <= unsupportedValue && item["max"].asInt() >= unsupportedValue) {
                    reg.CheckUnsupported = false;
                }
            }
        }
        return reg;
    }
} // namespace

void TRPCDeviceParametersCache::RegisterCallbacks(PHandlerConfig handlerConfig)
{
    for (const auto& portConfig: handlerConfig->PortConfigs) {
        for (const auto& device: portConfig->Devices) {
            std::string id = GetId(*portConfig->Port, device->Device->DeviceConfig()->SlaveId);
            device->Device->AddOnConnectionStateChangedCallback([this, id](PSerialDevice device) {
                if (device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) {
                    Remove(id);
                }
            });
        }
    }
}

std::string TRPCDeviceParametersCache::GetId(const TPort& port, const std::string& slaveId) const
{
    return port.GetDescription(false) + ":" + slaveId;
}

void TRPCDeviceParametersCache::Add(const std::string& id, const Json::Value& value)
{
    std::unique_lock lock(Mutex);
    DeviceParameters[id] = value;
}

void TRPCDeviceParametersCache::Remove(const std::string& id)
{
    std::unique_lock lock(Mutex);
    DeviceParameters.erase(id);
}

bool TRPCDeviceParametersCache::Contains(const std::string& id) const
{
    std::unique_lock lock(Mutex);
    return DeviceParameters.find(id) != DeviceParameters.end();
}

const Json::Value& TRPCDeviceParametersCache::Get(const std::string& id, const Json::Value& defaultValue) const
{
    std::unique_lock lock(Mutex);
    auto it = DeviceParameters.find(id);
    return it != DeviceParameters.end() ? it->second : defaultValue;
};

#ifndef __EMSCRIPTEN__
TRPCDeviceHelper::TRPCDeviceHelper(const Json::Value& request,
                                   const TSerialDeviceFactory& deviceFactory,
                                   PTemplateMap templates,
                                   TSerialClientTaskRunner& serialClientTaskRunner)
{
    auto params = serialClientTaskRunner.GetSerialClientParams(request);
    if (params.Device == nullptr) {
        DeviceTemplate = templates->GetTemplate(request["device_type"].asString());
        auto protocolName = DeviceTemplate->GetProtocol();
        if (protocolName == "modbus" && request["modbus_mode"].asString() == "TCP") {
            protocolName += "-tcp";
        }
        ProtocolParams = deviceFactory.GetProtocolParams(protocolName);
        auto config = std::make_shared<TDeviceConfig>("RPC Device", request["slave_id"].asString(), protocolName);
        bool isWbDevice = !DeviceTemplate->GetHardware().empty() ||
                          DeviceTemplate->GetTemplate()["enable_wb_continuous_read"].asBool();
        LoadCommonDeviceParameters(*config, DeviceTemplate->GetTemplate(), isWbDevice);
        Device = ProtocolParams.factory->CreateDevice(DeviceTemplate->GetTemplate(), config, ProtocolParams.protocol);
        Device->SetWbDevice(isWbDevice);
    } else {
        Device = params.Device;
        DeviceTemplate = templates->GetTemplate(Device->DeviceConfig()->DeviceType);
        ProtocolParams = deviceFactory.GetProtocolParams(Device->Protocol()->GetName());
        DeviceFromConfig = true;
    }
    if (DeviceTemplate->WithSubdevices()) {
        throw TRPCException("Device \"" + DeviceTemplate->Type + "\" is not supported by this RPC",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}
#endif

TRPCDeviceRequest::TRPCDeviceRequest(const TDeviceProtocolParams& protocolParams,
                                     PSerialDevice device,
                                     PDeviceTemplate deviceTemplate,
                                     bool deviceFromConfig)
    : ProtocolParams(protocolParams),
      Device(device),
      DeviceTemplate(deviceTemplate),
      DeviceFromConfig(deviceFromConfig)
{
    Json::Value responseTimeout = DeviceTemplate->GetTemplate()["response_timeout_ms"];
    if (responseTimeout.isInt()) {
        ResponseTimeout = std::chrono::milliseconds(responseTimeout.asInt());
    }

    Json::Value frameTimeout = DeviceTemplate->GetTemplate()["frame_timeout_ms"];
    if (frameTimeout.isInt()) {
        FrameTimeout = std::chrono::milliseconds(frameTimeout.asInt());
    }
}

void TRPCDeviceRequest::ParseSettings(const Json::Value& request,
                                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "response_timeout", ResponseTimeout);
    WBMQTT::JSON::Get(request, "frame_timeout", FrameTimeout);
    WBMQTT::JSON::Get(request, "total_timeout", TotalTimeout);
    OnResult = onResult;
    OnError = onError;
}

#ifndef __EMSCRIPTEN__
TRPCDeviceHandler::TRPCDeviceHandler(const std::string& configFileName,
                                     const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     const std::string& requestDeviceLoadSchemaFilePath,
                                     const std::string& requestDeviceSetSchemaFilePath,
                                     const std::string& requestDeviceProbeSchemaFilePath,
                                     const std::string& requestDeviceSetPollSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     PTemplateMap templates,
                                     TSerialClientTaskRunner& serialClientTaskRunner,
                                     TRPCDeviceParametersCache& parametersCache,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : ConfigFileName(configFileName),
      DeviceFactory(deviceFactory),
      RequestDeviceLoadConfigSchema(LoadRPCRequestSchema(requestDeviceLoadConfigSchemaFilePath, "device/LoadConfig")),
      RequestDeviceLoadSchema(LoadRPCRequestSchema(requestDeviceLoadSchemaFilePath, "device/Load")),
      RequestDeviceSetSchema(LoadRPCRequestSchema(requestDeviceSetSchemaFilePath, "device/Set")),
      RequestDeviceProbeSchema(LoadRPCRequestSchema(requestDeviceProbeSchemaFilePath, "device/Probe")),
      RequestDeviceSetPollSchema(LoadRPCRequestSchema(requestDeviceSetPollSchemaFilePath, "device/SetPoll")),
      Templates(templates),
      SerialClientTaskRunner(serialClientTaskRunner),
      ParametersCache(parametersCache)
{
    rpcServer->RegisterAsyncMethod("device",
                                   "LoadConfig",
                                   std::bind(&TRPCDeviceHandler::LoadConfig,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterAsyncMethod("device",
                                   "Load",
                                   std::bind(&TRPCDeviceHandler::Load, //
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterAsyncMethod("device",
                                   "Set",
                                   std::bind(&TRPCDeviceHandler::Set, //
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterAsyncMethod("device",
                                   "Probe",
                                   std::bind(&TRPCDeviceHandler::Probe,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));

    rpcServer->RegisterMethod("device", "SetPoll", std::bind(&TRPCDeviceHandler::SetPoll, this, std::placeholders::_1));
}

void TRPCDeviceHandler::LoadConfig(const Json::Value& request,
                                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                   WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestDeviceLoadConfigSchema);
    try {
        auto helper = TRPCDeviceHelper(request, DeviceFactory, Templates, SerialClientTaskRunner);
        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request,
                                                          helper.ProtocolParams,
                                                          helper.Device,
                                                          helper.DeviceTemplate,
                                                          helper.DeviceFromConfig,
                                                          ConfigFileName,
                                                          ParametersCache,
                                                          onResult,
                                                          onError);
        SerialClientTaskRunner.RunTask(request, std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCDeviceHandler::Load(const Json::Value& request,
                             WBMQTT::TMqttRpcServer::TResultCallback onResult,
                             WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestDeviceLoadSchema);
    try {
        auto helper = TRPCDeviceHelper(request, DeviceFactory, Templates, SerialClientTaskRunner);
        auto rpcRequest = ParseRPCDeviceLoadRequest(request,
                                                    helper.ProtocolParams,
                                                    helper.Device,
                                                    helper.DeviceTemplate,
                                                    helper.DeviceFromConfig,
                                                    onResult,
                                                    onError);
        SerialClientTaskRunner.RunTask(request, std::make_shared<TRPCDeviceLoadSerialClientTask>(rpcRequest));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCDeviceHandler::Set(const Json::Value& request,
                            WBMQTT::TMqttRpcServer::TResultCallback onResult,
                            WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestDeviceSetSchema);
    try {
        auto helper = TRPCDeviceHelper(request, DeviceFactory, Templates, SerialClientTaskRunner);
        auto rpcRequest = ParseRPCDeviceSetRequest(request,
                                                   helper.ProtocolParams,
                                                   helper.Device,
                                                   helper.DeviceTemplate,
                                                   helper.DeviceFromConfig,
                                                   onResult,
                                                   onError);
        SerialClientTaskRunner.RunTask(request, std::make_shared<TRPCDeviceSetSerialClientTask>(rpcRequest));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCDeviceHandler::Probe(const Json::Value& request,
                              WBMQTT::TMqttRpcServer::TResultCallback onResult,
                              WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ValidateRPCRequest(request, RequestDeviceProbeSchema);
    try {
        SerialClientTaskRunner.RunTask(request,
                                       std::make_shared<TRPCDeviceProbeSerialClientTask>(request, onResult, onError));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCDeviceHandler::SetPoll(const Json::Value& request)
{
    ValidateRPCRequest(request, RequestDeviceSetPollSchema);
    auto params = SerialClientTaskRunner.GetSerialClientParams(request);
    if (!params.SerialClient || !params.Device) {
        throw TRPCException("Port or device not found", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    try {
        if (!request["poll"].asBool()) {
            params.SerialClient->SuspendPoll(params.Device, std::chrono::steady_clock::now());
        } else {
            params.SerialClient->ResumePoll(params.Device);
        }
    } catch (const std::runtime_error& e) {
        LOG(Warn) << e.what();
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    return Json::Value(Json::objectValue);
}
#endif

void PrepareSession(TPort& port, PSerialDevice device, int maxRetries)
{
    for (int i = 0; i <= maxRetries; i++) {
        try {
            device->Prepare(port, TDevicePrepareMode::WITHOUT_SETUP);
            break;
        } catch (const TSerialDeviceException& e) {
            if (i == maxRetries) {
                auto error = std::string("Failed to prepare session: ") + e.what();
                LOG(Warn) << port.GetDescription() << " " << device->ToString() << ": " << error;
                throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
    }
}

void EndSession(TPort& port, PSerialDevice device)
{
    try {
        device->EndSession(port);
    } catch (const TSerialDeviceException& e) {
        LOG(Warn) << port.GetDescription() << " " << device->ToString() << " unable to end session: " << e.what();
    }
}

TRPCRegisterList CreateChannelsRegisterList(const TDeviceProtocolParams& protocolParams,
                                            PSerialDevice device,
                                            const Json::Value& channels)
{
    // nullptr checks are needed for tests
    auto fwVersion = device ? device->GetWbFwVersion() : std::string();
    auto checkUnsupported = device && device->IsWbDevice();

    TRPCRegisterList registerList;
    for (const auto& item: channels) {
        if (item["address"].isNull() || !IsSupportedByFw(item, fwVersion)) {
            continue;
        }
        registerList.push_back(
            CreateRPCRegister(protocolParams, device, item["id"].asString(), item, checkUnsupported));
    }
    return registerList;
}

TRPCRegisterList CreateParametersRegisterList(const TDeviceProtocolParams& protocolParams,
                                              PSerialDevice device,
                                              const Json::Value& parameters,
                                              const Json::Value& knownValues,
                                              const std::set<std::string>& ids)
{
    // nullptr checks are needed for tests
    auto fwVersion = device ? device->GetWbFwVersion() : std::string();
    auto checkUnsupported = device && device->IsWbDevice();

    TRPCRegisterList registerList;
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        const auto& item = *it;
        auto id = parameters.isObject() ? it.key().asString() : item["id"].asString();
        if (item["address"].isNull() || !knownValues[id].isNull()) {
            continue;
        }
        if (!ids.empty() && !ids.count(id)) {
            continue;
        }
        if (!IsSupportedByFw(item, fwVersion)) {
            continue;
        }
        registerList.push_back(CreateRPCRegister(protocolParams, device, id, item, checkUnsupported));
    }
    return registerList;
}

void ReadRegisterList(TPort& port, PSerialDevice device, TRPCRegisterList& registerList, int maxRetries)
{
    if (registerList.size() == 0) {
        return;
    }

    TRegisterComparePredicate compare;
    std::sort(registerList.begin(), registerList.end(), [compare](TRPCRegister& a, TRPCRegister& b) {
        return compare(b.Register, a.Register);
    });

    size_t index = 0;
    std::string error;
    while (index < registerList.size() && error.empty()) {
        auto first = registerList[index].Register;
        auto range = device->CreateRegisterRange();
        while (index < registerList.size() &&
               range->Add(port, registerList[index].Register, std::chrono::milliseconds::max()))
        {
            ++index;
        }
        for (int i = 0; i <= maxRetries; ++i) {
            try {
                device->ReadRegisterRange(port, range, true);
                break;
            } catch (const TSerialDevicePermanentRegisterException& e) {
                LOG(Warn) << port.GetDescription() << " " << device->ToString() << ": "
                          << "Failed to read " << std::to_string(range->RegisterList().size())
                          << " registers starting from <" << first->GetConfig()->ToString() + ">: " + e.what();
                auto modbusDevice = dynamic_cast<TModbusDevice*>(device.get());
                if (modbusDevice != nullptr &&
                    modbusDevice->GetContinuousReadStatus() == TContinuousReadStatus::DISABLED)
                {
                    for (const auto& reg: range->RegisterList()) {
                        reg->SetSupported(false);
                    }
                }
                break;
            } catch (const TSerialDeviceException& e) {
                if (i == maxRetries) {
                    error = "Failed to read " + std::to_string(range->RegisterList().size()) +
                            " registers starting from <" + first->GetConfig()->ToString() + ">: " + e.what();
                }
            }
        }
    }

    if (!error.empty()) {
        LOG(Warn) << port.GetDescription() << " " << device->ToString() << ": " << error;
        throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val)
{
    auto str = ConvertFromRawValue(reg, val);
    try {
        if (str.find('.') == std::string::npos) {
            if (str.at(0) == '-') {
                return static_cast<Json::Int64>(std::stoll(str.c_str(), 0));
            }
            auto value = std::stoull(str.c_str(), 0);
            if (value <= INT64_MAX) {
                // cast value to signed integer to match default Json::Value type for integers
                return static_cast<Json::Int64>(value);
            }
            return static_cast<Json::UInt64>(value);
        }
        return std::stod(str.c_str(), 0);
    } catch (const std::invalid_argument&) {
        return str;
    }
}

bool RegisterGotValue(const TRPCRegister& item)
{
    return item.Register->IsSupported() && !item.Register->GetErrorState().test(TRegister::TError::ReadError) &&
           item.Register->GetValue().GetType() != TRegisterValue::ValueType::Undefined;
}

void MergeRegisterListValues(const TRPCRegisterList& registerList, Json::Value& values)
{
    for (const auto& item: registerList) {
        if (RegisterGotValue(item)) {
            values[item.Id] = RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue());
        }
    }
}
