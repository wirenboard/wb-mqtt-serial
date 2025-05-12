#pragma once
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "confed_device_schemas_map.h"
#include "confed_protocol_schemas_map.h"
#include "serial_config.h"
class TRPCConfigHandler
{
public:
    TRPCConfigHandler(const std::string& configPath,
                      const Json::Value& portsSchema,
                      PTemplateMap templates,
                      TDevicesConfedSchemasMap& deviceConfedSchemas,
                      TProtocolConfedSchemasMap& protocolConfedSchemas,
                      const Json::Value& groupTranslations,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    std::string ConfigPath;
    const Json::Value& PortsSchema;
    PTemplateMap Templates;
    TDevicesConfedSchemasMap& DeviceConfedSchemas;
    TProtocolConfedSchemasMap& ProtocolConfedSchemas;
    Json::Value GroupTranslations;

    Json::Value LoadConfig(const Json::Value& request);
    Json::Value GetDeviceTypes(const Json::Value& request);
    Json::Value GetSchema(const Json::Value& request);
};

typedef std::shared_ptr<TRPCConfigHandler> PRPCConfigHandler;
