#include "rpc_device_handler.h"
#include "rpc_device_load_config_task.h"
#include "rpc_device_probe_task.h"
#include "rpc_helpers.h"

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
                                     const std::string& requestDeviceProbeSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     PTemplateMap templates,
                                     TSerialClientTaskRunner& serialClientTaskRunner,
                                     TRPCDeviceParametersCache& parametersCache,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : DeviceFactory(deviceFactory),
      RequestDeviceLoadConfigSchema(LoadRPCRequestSchema(requestDeviceLoadConfigSchemaFilePath, "device/LoadConfig")),
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
