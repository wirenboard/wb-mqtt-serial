#ifndef __EMSCRIPTEN__

#include "rpc_config_change_handler.h"

#include "json_common.h"
#include "log.h"
#include "rpc_device_type_json.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    Json::Value MakeDevicesJson(const std::vector<TConfigDeviceRef>& devices)
    {
        Json::Value res(Json::arrayValue);
        for (const auto& device: devices) {
            Json::Value item;
            item["port"] = device.Port;
            item["slave_id"] = device.SlaveId;
            res.append(std::move(item));
        }
        return res;
    }

    std::string MakeProblemsJson(const TApplyProblems& problems)
    {
        Json::Value res;
        res["disabled_devices"] = MakeDevicesJson(problems.DisabledDevices);
        res["unread_devices"] = MakeDevicesJson(problems.UnreadDevices);
        res["invalid_devices"] = MakeDevicesJson(problems.InvalidDevices);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, res);
    }

    //! Logs the error of the RPC and returns its "error.data"
    std::string ProcessApplyConfigError(const TApplyConfigError& e, const std::string& rpcName)
    {
        switch (e.GetReason()) {
            case TApplyConfigError::TReason::Busy:
                LOG(Debug) << rpcName << ": " << e.what();
                return CONFIG_BUSY_ERROR;
            case TApplyConfigError::TReason::ConfigInvalid:
                LOG(Debug) << rpcName << ": the configuration is rejected: " << e.what();
                return std::string("config-invalid: ") + e.what();
            case TApplyConfigError::TReason::PortTasksTimeout:
                LOG(Debug) << rpcName << ": " << e.what();
                return "port-busy";
            case TApplyConfigError::TReason::WriteFailed:
                LOG(Error) << rpcName << ": " << e.what();
                return "write-failed";
            case TApplyConfigError::TReason::PollingRestartFailed:
                LOG(Error) << rpcName << ": " << e.what();
                return "polling-restart-failed";
            case TApplyConfigError::TReason::TemplateInUse:
                LOG(Warn) << rpcName << ": " << e.what();
                return "template-in-use";
            case TApplyConfigError::TReason::ApplyProblems:
                LOG(Warn) << rpcName << ": the template is not applied: " << e.what();
                return "apply-problems: " + MakeProblemsJson(e.GetProblems());
        }
        return CONFIG_BUSY_ERROR;
    }
}

TRPCConfigChangeHandler::TRPCConfigChangeHandler(const Json::Value& groupTranslations,
                                                 const std::string& requestUploadSchemaFilePath,
                                                 const std::string& requestDeleteSchemaFilePath,
                                                 TSerialDriverCore& serialDriverCore)
    : GroupTranslations(groupTranslations),
      RequestUploadTemplateSchema(LoadRPCRequestSchema(requestUploadSchemaFilePath, "templates/Upload")),
      RequestDeleteTemplateSchema(LoadRPCRequestSchema(requestDeleteSchemaFilePath, "templates/Delete")),
      SerialDriverCore(serialDriverCore)
{}

Json::Value TRPCConfigChangeHandler::MakeSingleDeviceTypeResponse(const PDeviceTemplate& deviceTemplate,
                                                                  const std::string& lang)
{
    auto group = deviceTemplate->GetGroup().empty() ? CUSTOM_GROUP_NAME : deviceTemplate->GetGroup();
    Json::Value groupJson;
    groupJson["name"] = GetGroupTranslation(group, lang, GroupTranslations);
    MakeArray("types", groupJson).append(MakeDeviceTypeJson(deviceTemplate, lang));
    Json::Value res;
    MakeArray("types", res).append(std::move(groupJson));
    return res;
}

Json::Value TRPCConfigChangeHandler::MakeResponse(const TTemplateApplyResult& result, const Json::Value& request)
{
    auto lang = request.get("lang", "en").asString();
    Json::Value res;
    if (result.Template) {
        res = MakeSingleDeviceTypeResponse(result.Template, lang);
    } else {
        MakeArray("types", res);
    }
    res["config"] = result.Config;
    return res;
}

Json::Value TRPCConfigChangeHandler::SaveConfig(const Json::Value& request)
{
    if (!request["config"].isObject()) {
        throw std::runtime_error("\"config\" object is required");
    }
    try {
        SerialDriverCore.ApplyConfig(request["config"]);
    } catch (const TApplyConfigError& e) {
        throw std::runtime_error(ProcessApplyConfigError(e, "config/Save"));
    }
    return Json::Value(Json::objectValue);
}

Json::Value TRPCConfigChangeHandler::UploadTemplate(const Json::Value& request)
{
    ValidateRPCRequest(request, RequestUploadTemplateSchema);
    try {
        return MakeResponse(SerialDriverCore.UploadTemplate(request["content"].asString(),
                                                            request["filename"].asString(),
                                                            request.get("force", false).asBool()),
                            request);
    } catch (const TApplyConfigError& e) {
        throw std::runtime_error(ProcessApplyConfigError(e, "templates/Upload"));
    }
}

Json::Value TRPCConfigChangeHandler::DeleteTemplate(const Json::Value& request)
{
    ValidateRPCRequest(request, RequestDeleteTemplateSchema);
    try {
        return MakeResponse(
            SerialDriverCore.DeleteTemplate(request["type"].asString(), request.get("force", false).asBool()),
            request);
    } catch (const TApplyConfigError& e) {
        throw std::runtime_error(ProcessApplyConfigError(e, "templates/Delete"));
    }
}

void RegisterConfigChangeRpcHandlers(TRPCConfigChangeHandler& handler, WBMQTT::PMqttRpcServer rpcServer)
{
    rpcServer->RegisterMethod("config",
                              "Save",
                              std::bind(&TRPCConfigChangeHandler::SaveConfig, &handler, std::placeholders::_1));
    rpcServer->RegisterMethod("templates",
                              "Upload",
                              std::bind(&TRPCConfigChangeHandler::UploadTemplate, &handler, std::placeholders::_1));
    rpcServer->RegisterMethod("templates",
                              "Delete",
                              std::bind(&TRPCConfigChangeHandler::DeleteTemplate, &handler, std::placeholders::_1));
}

#endif
