#include "rpc_device_handler.h"
#include "rpc_device_load_config_handler.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCDeviceHandler::TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     std::shared_ptr<TTemplateMap> templates,
                                     PRPCConfig rpcConfig,
                                     WBMQTT::PMqttRpcServer rpcServer,
                                     PMQTTSerialDriver serialDriver)
    : DeviceFactory(deviceFactory),
      Templates(templates),
      RPCConfig(rpcConfig)
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

    PortDrivers = std::make_shared<TRPCPortDriverList>(rpcConfig, serialDriver);
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

    auto deviceTemplate = Templates->GetTemplate(request["device_type"].asString());
    // TODO: check template->GetProtocol() is modbus?
    // TODO: check template->WithSubdevices() is false

    Json::Value parameters = deviceTemplate->GetTemplate()["parameters"];
    // TODO: check parametets isn't empty

    try {
        PRPCPortDriver driver = PortDrivers->Find(request);

        if (driver != nullptr && driver->SerialClient) {
            RPCDeviceLoadConfigHandler(request, parameters, DeviceFactory, driver->SerialClient, onResult, onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCDeviceLoadConfigHandler(request, parameters, DeviceFactory, *port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}