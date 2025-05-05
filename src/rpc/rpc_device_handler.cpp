#include "rpc_device_handler.h"
#include "rpc_device_load_config_handler.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCDeviceHandler::TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     const TSerialDeviceFactory& deviceFactory,
                                     std::shared_ptr<TTemplateMap> templates,
                                     PRPCConfig rpcConfig,
                                     PHandlerConfig handlerConfig,
                                     WBMQTT::PMqttRpcServer rpcServer,
                                     PMQTTSerialDriver serialDriver)
    : DeviceFactory(deviceFactory),
      Templates(templates),
      RPCConfig(rpcConfig),
      HandlerConfig(handlerConfig),
      PortDrivers(rpcConfig, serialDriver)
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

PSerialDevice TRPCDeviceHandler::FindDevice(PPort port, const std::string& slaveId, const std::string& deviceType)
{
    for (const auto& portConfig: HandlerConfig->PortConfigs) {
        if (portConfig->Port != port) {
            continue;
        }
        for (const auto& device: portConfig->Devices) {
            auto deviceConfig = device->DeviceConfig();
            if (deviceConfig->SlaveId == slaveId && deviceConfig->DeviceType == deviceType) {
                return device;
            }
        }
    }

    return nullptr;
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
        PRPCPortDriver driver = PortDrivers.Find(request);
        if (driver != nullptr && driver->SerialClient) {
            auto device = FindDevice(driver->RPCPort->GetPort(), request["slave_id"].asString(), deviceType);
            RPCDeviceLoadConfigHandler(request,
                                       DeviceFactory,
                                       deviceTemplate,
                                       device,
                                       driver->SerialClient,
                                       onResult,
                                       onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCDeviceLoadConfigHandler(request, DeviceFactory, deviceTemplate, nullptr, port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}
