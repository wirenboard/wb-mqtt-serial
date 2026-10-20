#pragma once

#ifndef __EMSCRIPTEN__

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "serial_driver_core.h"
#include "templates_map.h"

class TRPCConfigChangeHandler
{
public:
    TRPCConfigChangeHandler(const Json::Value& groupTranslations,
                            const std::string& requestUploadSchemaFilePath,
                            const std::string& requestDeleteSchemaFilePath,
                            TSerialDriverCore& serialDriverCore);

    Json::Value SaveConfig(const Json::Value& request);
    Json::Value UploadTemplate(const Json::Value& request);
    Json::Value DeleteTemplate(const Json::Value& request);

private:
    Json::Value MakeResponse(const TTemplateApplyResult& result, const Json::Value& request);
    Json::Value MakeSingleDeviceTypeResponse(const PDeviceTemplate& deviceTemplate, const std::string& lang);

    Json::Value GroupTranslations;
    Json::Value RequestUploadTemplateSchema;
    Json::Value RequestDeleteTemplateSchema;
    TSerialDriverCore& SerialDriverCore;
};

typedef std::shared_ptr<TRPCConfigChangeHandler> PRPCConfigChangeHandler;

//! Handler must outlive rpcServer usage: registered methods keep a reference to it.
//! The methods block rpcServer until the change ends, so it must serve only the configuration changes
void RegisterConfigChangeRpcHandlers(TRPCConfigChangeHandler& handler, WBMQTT::PMqttRpcServer rpcServer);

#endif
