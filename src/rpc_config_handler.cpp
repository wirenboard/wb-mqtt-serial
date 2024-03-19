#include "rpc_config_handler.h"
#include "confed_json_generator.h"
#include "wblib/exceptions.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    Json::Value MakeDeviceTypeJson(const PDeviceTemplate& dt, const std::string& lang)
    {
        Json::Value res;
        res["name"] = dt->GetTitle(lang);
        res["deprecated"] = dt->IsDeprecated();
        res["type"] = dt->Type;
        return res;
    }

    Json::Value MakeDeviceTypeList(const TDeviceTypeGroup::TemplatesArray& templates, const std::string& lang)
    {
        Json::Value res(Json::arrayValue);
        std::for_each(templates.cbegin(), templates.cend(), [&res, &lang](const auto& t) {
            res.append(MakeDeviceTypeJson(t, lang));
        });
        return res;
    }

    Json::Value MakeDeviceGroupJson(const TDeviceTypeGroup& group, const std::string& lang)
    {
        Json::Value res;
        res["name"] = group.Name;
        res["types"] = MakeDeviceTypeList(group.Templates, lang);
        return res;
    }
}

TRPCConfigHandler::TRPCConfigHandler(const std::string& configPath,
                                     const std::string& portsSchemaPath,
                                     std::shared_ptr<Json::Value> configSchema,
                                     std::shared_ptr<TTemplateMap> templates,
                                     TDevicesConfedSchemasMap& confedSchemas,
                                     const Json::Value& groupTranslations,
                                     TSerialDeviceFactory& deviceFactory,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : ConfigPath(configPath),
      PortsSchemaPath(portsSchemaPath),
      ConfigSchema(configSchema),
      Templates(templates),
      ConfedSchemas(confedSchemas),
      GroupTranslations(groupTranslations),
      DeviceFactory(deviceFactory)
{
    rpcServer->RegisterMethod("config", "Load", std::bind(&TRPCConfigHandler::LoadConfig, this, std::placeholders::_1));
    rpcServer->RegisterMethod("config",
                              "GetSchema",
                              std::bind(&TRPCConfigHandler::GetSchema, this, std::placeholders::_1));
}

Json::Value TRPCConfigHandler::LoadConfig(const Json::Value& request)
{
    Json::Value res;
    res["config"] = MakeJsonForConfed(ConfigPath, *ConfigSchema, *Templates, DeviceFactory);
    res["schema"] = WBMQTT::JSON::Parse(PortsSchemaPath);
    res["types"] = GetDeviceTypes(request);
    return res;
}

Json::Value TRPCConfigHandler::GetDeviceTypes(const Json::Value& request)
{
    std::string lang(request.get("lang", "en").asString());
    Json::Value res(Json::arrayValue);
    auto templateGroups = OrderTemplates(Templates->GetTemplates(), GroupTranslations, lang);
    std::for_each(templateGroups.cbegin(), templateGroups.cend(), [&res, &lang](const auto& group) {
        res.append(MakeDeviceGroupJson(group, lang));
    });
    return res;
}

Json::Value TRPCConfigHandler::GetSchema(const Json::Value& request)
{
    return ConfedSchemas.GetSchema(request.get("type", "").asString());
}
