#pragma once
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "confed_device_schemas_map.h"
#include "confed_protocol_schemas_map.h"
#include "serial_config.h"

#ifndef __EMSCRIPTEN__
#include <thread>

#include "serial_driver_core.h"
#endif

class TRPCConfigHandler
{
public:
#ifndef __EMSCRIPTEN__
    TRPCConfigHandler(const std::string& configPath,
                      const Json::Value& portsSchema,
                      PTemplateMap templates,
                      TDevicesConfedSchemasMap& deviceConfedSchemas,
                      TProtocolConfedSchemasMap& protocolConfedSchemas,
                      const Json::Value& groupTranslations,
                      TSerialDriverCore& serialDriverCore,
                      WBMQTT::PMqttRpcServer rpcServer);
    ~TRPCConfigHandler();
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

    std::mutex SaveMutex;
    bool SaveInProgress = false;
    std::thread SaveThread;

    void SaveConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
#endif
};

typedef std::shared_ptr<TRPCConfigHandler> PRPCConfigHandler;
