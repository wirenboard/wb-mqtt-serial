#include "rpc_device_handler.h"
#include "rpc_device_load_config_task.h"
#include "rpc_device_load_task.h"
#include "rpc_device_probe_task.h"
#include "rpc_device_set_task.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

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
        if (ProtocolParams.protocol->IsModbus()) {
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;
        }
        Device = ProtocolParams.factory->CreateDevice(DeviceTemplate->GetTemplate(), config, ProtocolParams.protocol);
        Device->SetWbDevice(!DeviceTemplate->GetHardware().empty() ||
                            DeviceTemplate->GetTemplate()["enable_wb_continuous_read"].asBool());
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

TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& templateItems,
                                    const Json::Value& knownItems,
                                    const std::string& fwVersion,
                                    bool checkUnsupported)
{
    TRPCRegisterList registerList;
    for (auto it = templateItems.begin(); it != templateItems.end(); ++it) {
        const auto& item = *it;
        auto id = templateItems.isObject() ? it.key().asString() : item["id"].asString();
        if (item["address"].isNull() || item["readonly"].asBool() || !knownItems[id].isNull()) {
            continue;
        }
        if (!fwVersion.empty()) {
            std::string fw = item["fw"].asString();
            if (!fw.empty() && util::CompareVersionStrings(fw, fwVersion) > 0) {
                continue;
            }
        }
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
                const auto& list = item["enum"];
                for (auto it = list.begin(); it != list.end(); ++it) {
                    if ((*it).asInt() == unsupportedValue) {
                        reg.CheckUnsupported = false;
                        break;
                    }
                }
            } else {
                if (item["min"].asInt() <= unsupportedValue && item["max"].asInt() >= unsupportedValue) {
                    reg.CheckUnsupported = false;
                }
            }
        }

        registerList.push_back(reg);
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
                break;
            } catch (const TSerialDeviceException& e) {
                if (i == maxRetries) {
                    error = "Failed to read " + std::to_string(range->RegisterList().size()) +
                            " registers starting from <" + first->GetConfig()->ToString() + ">: " + e.what();
                }
            }
        }
    }

    try {
        device->EndSession(port);
    } catch (const TSerialDeviceException& e) {
        LOG(Warn) << port.GetDescription() << " " << device->ToString() << " unable to end session: " << e.what();
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
        return std::stod(str.c_str(), 0);
    } catch (const std::invalid_argument&) {
        return str;
    }
}
