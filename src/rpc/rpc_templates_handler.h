#pragma once

#ifndef __EMSCRIPTEN__

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "confed_device_schemas_map.h"
#include "templates_map.h"

class TRPCTemplatesHandler
{
public:
    TRPCTemplatesHandler(const std::string& userTemplatesDir,
                         const std::string& configPath,
                         PTemplateMap templates,
                         TDevicesConfedSchemasMap& deviceConfedSchemas,
                         const Json::Value& groupTranslations,
                         const std::string& requestUploadSchemaFilePath,
                         const std::string& requestDeleteSchemaFilePath);

    Json::Value UploadTemplate(const Json::Value& request);
    Json::Value DeleteTemplate(const Json::Value& request);

private:
    void CheckTemplateIsNotInUse(const std::string& deviceType, bool force);
    Json::Value MakeSingleDeviceTypeResponse(const PDeviceTemplate& deviceTemplate, const std::string& lang);

    std::string UserTemplatesDir;
    std::string ConfigPath;
    PTemplateMap Templates;
    TDevicesConfedSchemasMap& DeviceConfedSchemas;
    Json::Value GroupTranslations;
    Json::Value RequestUploadTemplateSchema;
    Json::Value RequestDeleteTemplateSchema;
};

typedef std::shared_ptr<TRPCTemplatesHandler> PRPCTemplatesHandler;

//! Handler must outlive rpcServer usage: registered methods keep a reference to it
void RegisterTemplatesRpcHandlers(TRPCTemplatesHandler& handler, WBMQTT::PMqttRpcServer rpcServer);

#endif
