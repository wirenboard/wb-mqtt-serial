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
    SerialClient = params.SerialClient;
    if (SerialClient == nullptr) {
        TaskExecutor = serialClientTaskRunner.GetTaskExecutor(request);
    }
    if (params.Device == nullptr) {
        DeviceTemplate = templates->GetTemplate(request["device_type"].asString());
        auto config = std::make_shared<TDeviceConfig>("RPC Device",
                                                      request["slave_id"].asString(),
                                                      DeviceTemplate->GetProtocol());
        if (DeviceTemplate->GetProtocol() == "modbus") {
            config->MaxRegHole = Modbus::MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            config->MaxBitHole = Modbus::MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            config->MaxReadRegisters = Modbus::MAX_READ_REGISTERS;
        }
        ProtocolParams = deviceFactory.GetProtocolParams(DeviceTemplate->GetProtocol());
        Device = ProtocolParams.factory->CreateDevice(DeviceTemplate->GetTemplate(),
                                                      config,
                                                      SerialClient ? SerialClient->GetPort() : TaskExecutor->GetPort(),
                                                      ProtocolParams.protocol);
    } else {
        Device = params.Device;
        DeviceTemplate = templates->GetTemplate(Device->DeviceConfig()->DeviceType);
        ProtocolParams = deviceFactory.GetProtocolParams(DeviceTemplate->GetProtocol());
        DeviceFromConfig = true;
    }
    if (DeviceTemplate->WithSubdevices()) {
        throw TRPCException("Device \"" + DeviceTemplate->Type + "\" is not supported by this RPC",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

void TRPCDeviceHelper::RunTask(PSerialClientTask task)
{
    if (SerialClient) {
        SerialClient->AddTask(task);
    } else {
        TaskExecutor->AddTask(task);
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

TRPCDeviceHandler::TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     const std::string& requestDeviceLoadSchemaFilePath,
                                     const std::string& requestDeviceSetSchemaFilePath,
                                     const std::string& requestDeviceProbeSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     PTemplateMap templates,
                                     TSerialClientTaskRunner& serialClientTaskRunner,
                                     TRPCDeviceParametersCache& parametersCache,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : DeviceFactory(deviceFactory),
      RequestDeviceLoadConfigSchema(LoadRPCRequestSchema(requestDeviceLoadConfigSchemaFilePath, "device/LoadConfig")),
      RequestDeviceLoadSchema(LoadRPCRequestSchema(requestDeviceLoadSchemaFilePath, "device/Load")),
      RequestDeviceSetSchema(LoadRPCRequestSchema(requestDeviceSetSchemaFilePath, "device/Set")),
      RequestDeviceProbeSchema(LoadRPCRequestSchema(requestDeviceProbeSchemaFilePath, "device/Probe")),
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
                                                          ParametersCache,
                                                          onResult,
                                                          onError);
        helper.RunTask(std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
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
        helper.RunTask(std::make_shared<TRPCDeviceLoadSerialClientTask>(rpcRequest));
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
        helper.RunTask(std::make_shared<TRPCDeviceSetSerialClientTask>(rpcRequest));
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

TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& templateItems,
                                    const Json::Value& knownItems,
                                    const std::string& fwVersion)
{
    TRPCRegisterList registerList;
    for (auto it = templateItems.begin(); it != templateItems.end(); ++it) {
        const auto& item = *it;
        auto id = templateItems.isObject() ? it.key().asString() : item["id"].asString();
        bool duplicate = false;
        for (const auto& item: registerList) {
            if (item.first == id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate || item["address"].isNull() || item["readonly"].asBool() || !knownItems[id].isNull()) {
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
        auto reg = std::make_shared<TRegister>(device, config.RegisterConfig);
        reg->SetAvailable(TRegisterAvailability::AVAILABLE);
        registerList.push_back(std::make_pair(id, reg));
    }
    return registerList;
}

void ReadRegisterList(PSerialDevice device, TRPCRegisterList& registerList, Json::Value& result, int maxRetries)
{
    if (registerList.size() == 0) {
        return;
    }
    TRegisterComparePredicate compare;
    std::sort(registerList.begin(),
              registerList.end(),
              [compare](std::pair<std::string, PRegister>& a, std::pair<std::string, PRegister>& b) {
                  return compare(b.second, a.second);
              });

    std::string error;
    for (int i = 0; i <= maxRetries; i++) {
        try {
            device->Prepare(TDevicePrepareMode::WITHOUT_SETUP);
            break;
        } catch (const TSerialDeviceException& e) {
            if (i == maxRetries) {
                error = std::string("Failed to prepare session: ") + e.what();
                LOG(Warn) << device->ToString() << ": " << error;
                throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
            }
        }
    }

    size_t index = 0;
    while (index < registerList.size() && error.empty()) {
        auto range = device->CreateRegisterRange();
        auto offset = index;
        while (index < registerList.size() && range->Add(registerList[index].second, std::chrono::milliseconds::max()))
        {
            ++index;
        }
        error.clear();
        for (int i = 0; i <= maxRetries; ++i) {
            device->ReadRegisterRange(range);
            while (offset < index) {
                auto reg = registerList[offset++].second;
                if (reg->GetErrorState().count()) {
                    error = "Failed to read " + std::to_string(range->RegisterList().size()) +
                            " registers starting from <" + reg->GetConfig()->ToString() +
                            ">: " + reg->GetErrorDescription();
                    break;
                }
            }
            if (error.empty()) {
                break;
            }
        }
    }

    try {
        device->EndSession();
    } catch (const TSerialDeviceException& e) {
        LOG(Warn) << device->ToString() << " unable to end session: " << e.what();
    }

    if (!error.empty()) {
        LOG(Warn) << device->ToString() << ": " << error;
        throw TRPCException(error, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    for (size_t i = 0; i < registerList.size(); ++i) {
        auto& reg = registerList[i];
        result[reg.first] = RawValueToJSON(*reg.second->GetConfig(), reg.second->GetValue());
    }
}