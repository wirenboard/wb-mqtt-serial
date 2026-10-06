#include "rpc_config_handler.h"
#include "confed_json_generator.h"
#include "file_utils.h"
#include "json_common.h"
#include "log.h"
#include "rpc_device_type_json.h"
#include "rpc_exception.h"
#include "wblib/exceptions.h"

#ifndef __EMSCRIPTEN__
#include "rpc_helpers.h"
#endif

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const std::string PROTOCOL_PREFIX = "protocol:";

#ifndef __EMSCRIPTEN__
    //! Logs the error and returns its "error.data" code
    std::string ProcessApplyConfigError(const TApplyConfigError& e)
    {
        switch (e.GetReason()) {
            case TApplyConfigError::TReason::Busy:
                LOG(Debug) << "config/Save: " << e.what();
                return CONFIG_BUSY_ERROR;
            case TApplyConfigError::TReason::ConfigInvalid:
                LOG(Debug) << "config/Save: the configuration is rejected: " << e.what();
                return CONFIG_INVALID_ERROR + ": " + e.what();
            case TApplyConfigError::TReason::PortTasksTimeout:
                LOG(Debug) << "config/Save: " << e.what();
                return PORT_BUSY_ERROR;
            case TApplyConfigError::TReason::WriteFailed:
                LOG(Error) << "config/Save: " << e.what();
                return WRITE_FAILED_ERROR;
            case TApplyConfigError::TReason::PollingRestartFailed:
                LOG(Error) << "config/Save: " << e.what();
                return POLLING_RESTART_FAILED_ERROR;
        }
        return CONFIG_BUSY_ERROR;
    }
#endif

    struct TDeviceTypeGroup
    {
        typedef std::vector<PDeviceTemplate> TemplatesArray;

        std::string Name;
        TemplatesArray Templates;
    };

    Json::Value MakeProtocolJson(const TProtocolConfedSchema& schema, const std::string& lang)
    {
        Json::Value res;
        res["name"] = schema.GetTitle(lang);
        res["deprecated"] = false;
        res["type"] = PROTOCOL_PREFIX + schema.Type;
        res["protocol"] = schema.Type;
        res["mqtt-id"] = schema.Type;
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

#ifndef __EMSCRIPTEN__
TRPCConfigHandler::TRPCConfigHandler(const std::string& configPath,
                                     const Json::Value& portsSchema,
                                     PTemplateMap templates,
                                     TDevicesConfedSchemasMap& deviceConfedSchemas,
                                     TProtocolConfedSchemasMap& protocolConfedSchemas,
                                     const Json::Value& groupTranslations,
                                     TSerialDriverCore& serialDriverCore,
                                     WBMQTT::PMqttRpcServer rpcServer)
    : ConfigPath(configPath),
      PortsSchema(portsSchema),
      Templates(templates),
      DeviceConfedSchemas(deviceConfedSchemas),
      ProtocolConfedSchemas(protocolConfedSchemas),
      GroupTranslations(groupTranslations),
      SerialDriverCore(serialDriverCore)
{
    rpcServer->RegisterMethod("config", "Load", std::bind(&TRPCConfigHandler::LoadConfig, this, std::placeholders::_1));
    rpcServer->RegisterMethod("config",
                              "GetSchema",
                              std::bind(&TRPCConfigHandler::GetSchema, this, std::placeholders::_1));
    rpcServer->RegisterAsyncMethod("config",
                                   "Save",
                                   std::bind(&TRPCConfigHandler::SaveConfig,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
}

TRPCConfigHandler::~TRPCConfigHandler()
{
    if (SaveThread.joinable()) {
        SaveThread.join();
    }
}
#else
TRPCConfigHandler::TRPCConfigHandler(const Json::Value& portsSchema,
                                     PTemplateMap templates,
                                     TDevicesConfedSchemasMap& deviceConfedSchemas,
                                     TProtocolConfedSchemasMap& protocolConfedSchemas,
                                     const Json::Value& groupTranslations)
    : PortsSchema(portsSchema),
      Templates(templates),
      DeviceConfedSchemas(deviceConfedSchemas),
      ProtocolConfedSchemas(protocolConfedSchemas),
      GroupTranslations(groupTranslations)
{}
#endif

Json::Value TRPCConfigHandler::LoadConfig(const Json::Value& request)
{
#ifndef __EMSCRIPTEN__
    if (SerialDriverCore.IsBusy()) {
        throw TRPCException(CONFIG_BUSY_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
#endif
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
#ifndef __EMSCRIPTEN__
    if (SerialDriverCore.IsBusy()) {
        throw TRPCException(CONFIG_BUSY_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
#endif
    std::string type = request.get("type", "").asString();
    if (type.find(PROTOCOL_PREFIX) == 0) {
        type = type.substr(PROTOCOL_PREFIX.size());
        return ProtocolConfedSchemas.GetSchema(type);
    }
    try {
        return *DeviceConfedSchemas.GetSchema(type);
    } catch (const std::runtime_error& e) {
        LOG(Error) << e.what();
        throw TRPCException("Template \"" + type + "\" schema validation failed",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

#ifndef __EMSCRIPTEN__
void TRPCConfigHandler::SaveConfig(const Json::Value& request,
                                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                   WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (!request["config"].isObject()) {
        onError(WBMQTT::E_RPC_SERVER_ERROR, "\"config\" object is required");
        return;
    }
    std::lock_guard<std::mutex> lock(SaveMutex);
    if (SaveInProgress) {
        onError(WBMQTT::E_RPC_SERVER_ERROR, CONFIG_BUSY_ERROR);
        return;
    }
    if (SaveThread.joinable()) {
        SaveThread.join();
    }
    SaveInProgress = true;
    // The RPC server goes on answering other requests meanwhile
    SaveThread = std::thread([this, request, onResult, onError]() {
        std::string error;
        try {
            SerialDriverCore.ApplyConfig(request["config"]);
        } catch (const TApplyConfigError& e) {
            error = ProcessApplyConfigError(e);
        } catch (const std::exception& e) {
            error = e.what();
        }
        {
            std::lock_guard<std::mutex> lock(SaveMutex);
            SaveInProgress = false;
        }
        if (error.empty()) {
            onResult(Json::Value(Json::objectValue));
        } else {
            onError(WBMQTT::E_RPC_SERVER_ERROR, error);
        }
    });
}
#endif
