#include "rpc_device_handler.h"
#include "rpc_device_load_config_task.h"

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

TRPCDeviceHandler::TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     std::shared_ptr<TTemplateMap> templates,
                                     TSerialClientTaskRunner& serialClientTaskRunner,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : DeviceFactory(deviceFactory),
      Templates(templates),
      SerialClientTaskRunner(serialClientTaskRunner)
{
    try {
        RequestDeviceLoadConfigSchema = WBMQTT::JSON::Parse(requestDeviceLoadConfigSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "device/LoadConfig request schema reading error: " << e.what();
        throw;
    }

    rpcServer->RegisterAsyncMethod("device",
                                   "LoadConfig",
                                   std::bind(&TRPCDeviceHandler::LoadConfig,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
}

void TRPCDeviceHandler::LoadConfig(const Json::Value& request,
                                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                   WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        WBMQTT::JSON::Validate(request, RequestDeviceLoadConfigSchema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

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
        auto rpcRequest = ParseRPCDeviceLoadConfigRequest(request, parameters, DeviceFactory);
        SetCallbacks(*rpcRequest, onResult, onError);
        SerialClientTaskRunner.RunTask(request, std::make_shared<TRPCDeviceLoadConfigSerialClientTask>(rpcRequest));
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}
