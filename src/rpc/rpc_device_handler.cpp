#include "rpc_device_handler.h"
#include "rpc_device_load_config_handler.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCDeviceHandler::TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                                     PRPCConfig rpcConfig,
                                     WBMQTT::PMqttRpcServer rpcServer,
                                     PMQTTSerialDriver serialDriver)
    : RPCConfig(rpcConfig)
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

    try {
        PRPCPortDriver rpcPortDriver = PortDrivers->Find(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCDeviceLoadConfigHandler(request, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            auto port = InitPort(request);
            port->Open();
            RPCDeviceLoadConfigHandler(request, *port, onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }

    LOG(Info) << "Hello there!";
}