#include "rpc_config_handler.h"
#include "confed_json_generator.h"
#include "file_utils.h"
#include "json_common.h"
#include "log.h"
#include "wblib/exceptions.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const std::string CUSTOM_GROUP_NAME = "g-custom";
    const std::string WB_GROUP_NAME = "g-wb";
    const std::string WB_OLD_GROUP_NAME = "g-wb-old";

    struct TDeviceTypeGroup
    {
        typedef std::vector<PDeviceTemplate> TemplatesArray;

        std::string Name;
        TemplatesArray Templates;
    };

    Json::Value MakeDeviceTypeJson(const PDeviceTemplate& dt, const std::string& lang)
    {
        Json::Value res;
        res["name"] = dt->GetTitle(lang);
        res["deprecated"] = dt->IsDeprecated();
        res["type"] = dt->Type;
        res["protocol"] = dt->GetProtocol();
        res["mqtt-id"] = dt->GetMqttId();
        if (!dt->GetHardware().empty()) {
            auto& hwJsonArray = MakeArray("hw", res);
            for (const auto& hw: dt->GetHardware()) {
                Json::Value hwJson;
                hwJson["signature"] = hw.Signature;
                if (!hw.Fw.empty()) {
                    hwJson["fw"] = hw.Fw;
                }
                hwJsonArray.append(hwJson);
            }
        }
        return res;
    }

    Json::Value MakeProtocolJson(const TProtocolConfedSchema& schema, const std::string& lang)
    {
        Json::Value res;
        res["name"] = schema.GetTitle(lang);
        res["type"] = schema.Type;
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

    std::vector<Json::Value> GetProtocols(TProtocolConfedSchemasMap& protocolConfedSchemas, const std::string& lang)
    {
        std::vector<Json::Value> res;
        for (const auto& protocolSchema: protocolConfedSchemas.GetSchemas()) {
            res.emplace_back(MakeProtocolJson(protocolSchema.second, lang));
        }
        std::sort(res.begin(), res.end(), [](const auto& p1, const auto& p2) {
            return p1["name"].asString() < p2["name"].asString();
        });
        return res;
    }

    std::string GetGroupTranslation(const std::string& group,
                                    const std::string& lang,
                                    const Json::Value& groupTranslations)
    {
        auto res = groupTranslations.get(lang, Json::Value(Json::objectValue)).get(group, "").asString();
        return res.empty() ? group : res;
    }

    std::vector<TDeviceTypeGroup> OrderTemplates(const std::vector<PDeviceTemplate>& templates,
                                                 const Json::Value& groupTranslations,
                                                 const std::string& lang)
    {
        std::map<std::string, std::vector<PDeviceTemplate>> groups;
        std::vector<PDeviceTemplate> groupWb;
        std::vector<PDeviceTemplate> groupWbOld;

        for (const auto& templatePtr: templates) {
            const auto& group = templatePtr->GetGroup();
            if (group == WB_GROUP_NAME) {
                groupWb.push_back(templatePtr);
            } else if (group == WB_OLD_GROUP_NAME) {
                groupWbOld.push_back(templatePtr);
            } else if (group.empty()) {
                groups[CUSTOM_GROUP_NAME].push_back(templatePtr);
            } else {
                groups[group].push_back(templatePtr);
            }
        }

        auto titleSortFn = [&lang](const auto& t1, const auto& t2) { return t1->GetTitle(lang) < t2->GetTitle(lang); };
        std::for_each(groups.begin(), groups.end(), [&](auto& group) {
            std::sort(group.second.begin(), group.second.end(), titleSortFn);
        });
        std::sort(groupWb.begin(), groupWb.end(), titleSortFn);
        std::sort(groupWbOld.begin(), groupWbOld.end(), titleSortFn);

        std::vector<TDeviceTypeGroup> res;
        std::transform(groups.begin(), groups.end(), std::back_inserter(res), [&](auto& group) {
            return TDeviceTypeGroup{GetGroupTranslation(group.first, lang, groupTranslations), std::move(group.second)};
        });

        std::sort(res.begin(), res.end(), [](const auto& g1, const auto& g2) { return g1.Name < g2.Name; });
        res.insert(
            res.begin(),
            TDeviceTypeGroup{GetGroupTranslation(WB_OLD_GROUP_NAME, lang, groupTranslations), std::move(groupWbOld)});
        res.insert(res.begin(),
                   TDeviceTypeGroup{GetGroupTranslation(WB_GROUP_NAME, lang, groupTranslations), std::move(groupWb)});
        return res;
    }
}

TRPCConfigHandler::TRPCConfigHandler(const std::string& configPath,
                                     const Json::Value& portsSchema,
                                     std::shared_ptr<TTemplateMap> templates,
                                     TDevicesConfedSchemasMap& deviceConfedSchemas,
                                     TProtocolConfedSchemasMap& protocolConfedSchemas,
                                     const Json::Value& groupTranslations,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : ConfigPath(configPath),
      PortsSchema(portsSchema),
      Templates(templates),
      DeviceConfedSchemas(deviceConfedSchemas),
      ProtocolConfedSchemas(protocolConfedSchemas),
      GroupTranslations(groupTranslations)
{
    rpcServer->RegisterMethod("config", "Load", std::bind(&TRPCConfigHandler::LoadConfig, this, std::placeholders::_1));
    rpcServer->RegisterMethod("config",
                              "GetSchema",
                              std::bind(&TRPCConfigHandler::GetSchema, this, std::placeholders::_1));
}

Json::Value TRPCConfigHandler::LoadConfig(const Json::Value& request)
{
    Json::Value res;
    res["config"] = MakeJsonForConfed(ConfigPath, *Templates);
    res["schema"] = PortsSchema;
    res["types"] = GetDeviceTypes(request);
    return res;
}

Json::Value TRPCConfigHandler::GetDeviceTypes(const Json::Value& request)
{
    std::string lang(request.get("lang", "en").asString());
    Json::Value res(Json::arrayValue);
    auto templateGroups = OrderTemplates(Templates->GetTemplates(), GroupTranslations, lang);
    auto customGroupName = GetGroupTranslation(CUSTOM_GROUP_NAME, lang, GroupTranslations);
    bool customGroupIsMissing = true;
    std::for_each(templateGroups.cbegin(), templateGroups.cend(), [&](const auto& group) {
        auto groupJson = MakeDeviceGroupJson(group, lang);
        if (group.Name == customGroupName) {
            customGroupIsMissing = false;
            for (auto& protocolJson: GetProtocols(ProtocolConfedSchemas, lang)) {
                groupJson["types"].append(std::move(protocolJson));
            }
        }
        res.append(groupJson);
    });
    if (customGroupIsMissing) {
        Json::Value groupJson;
        groupJson["name"] = customGroupName;
        groupJson["types"] = Json::Value(Json::arrayValue);
        for (auto& protocolJson: GetProtocols(ProtocolConfedSchemas, lang)) {
            groupJson["types"].append(std::move(protocolJson));
        }
        res.append(groupJson);
    }
    return res;
}

Json::Value TRPCConfigHandler::GetSchema(const Json::Value& request)
{
    std::string type = request.get("type", "").asString();
    try {
        return *DeviceConfedSchemas.GetSchema(type);
    } catch (const std::out_of_range&) {
        return ProtocolConfedSchemas.GetSchema(type);
    }
}
