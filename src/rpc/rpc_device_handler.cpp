#include "rpc_device_handler.h"
#include "rpc_device_load_config_task.h"
#include "rpc_device_probe_task.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    void SetCallbacks(TRPCDeviceLoadConfigRequest& request,
                      WBMQTT::TMqttRpcServer::TResultCallback onResult,
                      WBMQTT::TMqttRpcServer::TErrorCallback onError)
    {
        request.OnResult = onResult;
        request.OnError = onError;
    }
}

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

    std::string deviceType = request["device_type"].asString();
    auto deviceTemplate = Templates->GetTemplate(deviceType);
    if (deviceTemplate->GetProtocol() != "modbus" || deviceTemplate->WithSubdevices()) {
        throw TRPCException("Device \"" + deviceType + "\" is not supported by this RPC",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    Json::Value parameters = deviceTemplate->GetTemplate()["parameters"];
    if (parameters.empty()) {
        onResult(Json::Value(Json::objectValue));
        return;
    }

    try {
        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request, DeviceFactory, deviceTemplate, ParametersCache);
        SetCallbacks(*rpcRequest, onResult, onError);
        SerialClientTaskRunner.RunTask(request, std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
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
