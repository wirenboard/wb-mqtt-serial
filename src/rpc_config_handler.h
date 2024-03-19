#pragma once
#include "serial_config.h"
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

class TRPCConfigHandler
{
public:
    TRPCConfigHandler(const std::string& configPath,
                      const std::string& portsSchemaPath,
                      std::shared_ptr<Json::Value> configSchema,
                      std::shared_ptr<TTemplateMap> templates,
                      TDevicesConfedSchemasMap& confedSchemas,
                      const Json::Value& groupTranslations,
                      TSerialDeviceFactory& deviceFactory,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    std::string ConfigPath;
    std::string PortsSchemaPath;
    std::shared_ptr<Json::Value> ConfigSchema;
    std::shared_ptr<TTemplateMap> Templates;
    TDevicesConfedSchemasMap& ConfedSchemas;
    Json::Value GroupTranslations;
    TSerialDeviceFactory& DeviceFactory;

    Json::Value LoadConfig(const Json::Value& request);
    Json::Value GetDeviceTypes(const Json::Value& request);
    Json::Value GetSchema(const Json::Value& request);
};

typedef std::shared_ptr<TRPCConfigHandler> PRPCConfigHandler;
