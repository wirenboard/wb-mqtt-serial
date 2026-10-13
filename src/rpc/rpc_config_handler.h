#pragma once
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "confed_device_schemas_map.h"
#include "confed_protocol_schemas_map.h"
#include "serial_config.h"

#ifndef __EMSCRIPTEN__
#include "serial_driver_core.h"
#endif

class TRPCConfigHandler
{
public:
#ifndef __EMSCRIPTEN__
    TRPCConfigHandler(const Json::Value& groupTranslations,
                      TSerialDriverCore& serialDriverCore,
                      WBMQTT::PMqttRpcServer rpcServer);
#else
    TRPCConfigHandler(const Json::Value& portsSchema,
                      PTemplateMap templates,
                      TDevicesConfedSchemasMap& deviceConfedSchemas,
                      TProtocolConfedSchemasMap& protocolConfedSchemas,
                      const Json::Value& groupTranslations);
#endif
    Json::Value GetDeviceTypes(const Json::Value& request);
    Json::Value GetSchema(const Json::Value& request);

private:
    std::string ConfigPath;
    const Json::Value& PortsSchema;
    PTemplateMap Templates;
    TDevicesConfedSchemasMap& DeviceConfedSchemas;
    TProtocolConfedSchemasMap& ProtocolConfedSchemas;
    Json::Value GroupTranslations;

    Json::Value LoadConfig(const Json::Value& request);

#ifndef __EMSCRIPTEN__
    TSerialDriverCore& SerialDriverCore;
#endif
};

typedef std::shared_ptr<TRPCConfigHandler> PRPCConfigHandler;
