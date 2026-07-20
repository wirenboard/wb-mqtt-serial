#ifndef __EMSCRIPTEN__

#include "rpc_templates_handler.h"

#include <filesystem>
#include <fstream>

#include <wblib/utils.h>

#include "file_utils.h"
#include "json_common.h"
#include "log.h"
#include "rpc_device_type_json.h"
#include "rpc_exception.h"
#include "rpc_helpers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const std::string TEMPLATE_IN_USE_ERROR = "template-in-use";
    const std::string JSON_SUFFIX = ".json";

    Json::Value ParseTemplateContent(const std::string& content)
    {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        Json::String errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(content.data(), content.data() + content.size(), &root, &errs)) {
            throw TRPCException("Failed to parse template JSON: " + errs, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }
        return root;
    }

    bool IsAllowedFilenameChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    }

    //! Takes basename, replaces characters not from [A-Za-z0-9._-] with '_' and adds ".json" suffix if needed
    std::string SanitizeFilename(const std::string& filename)
    {
        std::string name = std::filesystem::path(filename).filename().string();
        for (auto& c: name) {
            if (!IsAllowedFilenameChar(c)) {
                c = '_';
            }
        }
        if (!name.ends_with(JSON_SUFFIX)) {
            name += JSON_SUFFIX;
        }
        if (name == JSON_SUFFIX) {
            throw TRPCException("Invalid \"filename\": \"" + filename + "\"", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }
        return name;
    }

    size_t CountDevicesUsingType(const std::string& configPath, const std::string& deviceType)
    {
        Json::Value config;
        try {
            config = WBMQTT::JSON::Parse(configPath);
        } catch (const std::exception&) {
            // Missing or broken config can't refer to the template, nothing to protect
            return 0;
        }
        size_t count = 0;
        for (const auto& port: config["ports"]) {
            for (const auto& device: port["devices"]) {
                if (device["device_type"].isString() && device["device_type"].asString() == deviceType) {
                    ++count;
                }
            }
        }
        return count;
    }

    void WriteFileAtomically(const std::filesystem::path& path, const std::string& content)
    {
        auto tmpPath = path.string() + ".tmp";
        try {
            {
                std::ofstream file;
                OpenWithException(file, tmpPath);
                file << content;
                file.flush();
                if (!file.good()) {
                    throw std::runtime_error("Failed to write file: " + tmpPath);
                }
            }
            std::filesystem::rename(tmpPath, path);
        } catch (const std::exception& e) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            throw TRPCException("Failed to write template file: " + std::string(e.what()),
                                TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }
    }
}

TRPCTemplatesHandler::TRPCTemplatesHandler(const std::string& userTemplatesDir,
                                           const std::string& configPath,
                                           PTemplateMap templates,
                                           TDevicesConfedSchemasMap& deviceConfedSchemas,
                                           const Json::Value& groupTranslations,
                                           const std::string& requestUploadSchemaFilePath,
                                           const std::string& requestDeleteSchemaFilePath)
    : UserTemplatesDir(userTemplatesDir),
      ConfigPath(configPath),
      Templates(templates),
      DeviceConfedSchemas(deviceConfedSchemas),
      GroupTranslations(groupTranslations),
      RequestUploadTemplateSchema(LoadRPCRequestSchema(requestUploadSchemaFilePath, "templates/Upload")),
      RequestDeleteTemplateSchema(LoadRPCRequestSchema(requestDeleteSchemaFilePath, "templates/Delete"))
{}

void TRPCTemplatesHandler::CheckTemplateIsNotInUse(const std::string& deviceType, bool force)
{
    if (force) {
        return;
    }
    auto devicesCount = CountDevicesUsingType(ConfigPath, deviceType);
    if (devicesCount != 0) {
        LOG(Warn) << "Device type \"" << deviceType << "\" is used by " << devicesCount << " device(s) from "
                  << ConfigPath << ", \"force\" flag is required to modify its template";
        throw TRPCException(TEMPLATE_IN_USE_ERROR, TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

Json::Value TRPCTemplatesHandler::MakeSingleDeviceTypeResponse(const PDeviceTemplate& deviceTemplate,
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

Json::Value TRPCTemplatesHandler::UploadTemplate(const Json::Value& request)
{
    ValidateRPCRequest(request, RequestUploadTemplateSchema);
    auto lang = request.get("lang", "en").asString();
    auto force = request.get("force", false).asBool();
    auto content = request["content"].asString();

    auto templateRoot = ParseTemplateContent(content);
    if (!templateRoot.isObject()) {
        throw TRPCException("Template must be a JSON object", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    std::string deviceType;
    if (templateRoot["device_type"].isString()) {
        deviceType = templateRoot["device_type"].asString();
    }
    if (deviceType.empty()) {
        throw TRPCException("Template must contain a non-empty \"device_type\" property",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    // Deprecated templates are exempt from schema validation on disk load,
    // so accepting them would allow storing files the schema can't vouch for
    if (templateRoot.get("deprecated", false).asBool()) {
        throw TRPCException("Uploading of deprecated templates is not supported",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    try {
        Templates->ValidateTemplate(templateRoot);
    } catch (const std::exception& e) {
        throw TRPCException("Template validation failed: " + std::string(e.what()),
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    CheckTemplateIsNotInUse(deviceType, force);

    auto fileName = SanitizeFilename(request["filename"].asString());
    auto oldTemplate = Templates->FindUserDefinedTemplate(deviceType);
    auto oldPath = oldTemplate ? oldTemplate->GetFilePath() : std::string();
    auto finalPath = std::filesystem::path(UserTemplatesDir) / fileName;
    auto stem = fileName.substr(0, fileName.size() - JSON_SUFFIX.size());
    for (size_t suffix = 1; std::filesystem::exists(finalPath) && finalPath.string() != oldPath; ++suffix) {
        finalPath = std::filesystem::path(UserTemplatesDir) / (stem + "-" + std::to_string(suffix) + JSON_SUFFIX);
    }

    std::error_code ec;
    std::filesystem::create_directories(UserTemplatesDir, ec);
    WriteFileAtomically(finalPath, content);

    try {
        for (const auto& type: Templates->UpdateTemplate(finalPath.string())) {
            DeviceConfedSchemas.InvalidateCache(type);
        }
    } catch (const std::exception& e) {
        std::error_code removeEc;
        std::filesystem::remove(finalPath, removeEc);
        LOG(Warn) << "[templates] failed to load uploaded template, file " << finalPath.string()
                  << " is removed: " << e.what();
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    if (!oldPath.empty() && oldPath != finalPath.string()) {
        std::error_code removeEc;
        if (std::filesystem::remove(oldPath, removeEc) || !removeEc) {
            DeviceConfedSchemas.InvalidateCache(Templates->DeleteTemplate(oldPath));
        } else {
            // Keep the in-memory entry: the file is still on disk and will be loaded again after restart
            LOG(Warn) << "[templates] failed to remove old template file " << oldPath << ": " << removeEc.message();
        }
    }

    return MakeSingleDeviceTypeResponse(Templates->GetTemplate(deviceType), lang);
}

Json::Value TRPCTemplatesHandler::DeleteTemplate(const Json::Value& request)
{
    ValidateRPCRequest(request, RequestDeleteTemplateSchema);
    auto lang = request.get("lang", "en").asString();
    auto force = request.get("force", false).asBool();
    auto deviceType = request["type"].asString();

    auto userTemplate = Templates->FindUserDefinedTemplate(deviceType);
    if (!userTemplate) {
        throw TRPCException("There is no user-defined template for device type \"" + deviceType + "\"",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    CheckTemplateIsNotInUse(deviceType, force);

    auto path = userTemplate->GetFilePath();
    if (UserTemplatesDir.empty() || !WBMQTT::StringStartsWith(path, UserTemplatesDir)) {
        throw TRPCException("Template file \"" + path + "\" is not from user templates directory",
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    try {
        std::filesystem::remove(path);
    } catch (const std::filesystem::filesystem_error& e) {
        throw TRPCException("Failed to delete template file: " + std::string(e.what()),
                            TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
    DeviceConfedSchemas.InvalidateCache(Templates->DeleteTemplate(path));

    try {
        return MakeSingleDeviceTypeResponse(Templates->GetTemplate(deviceType), lang);
    } catch (const std::out_of_range&) {
        Json::Value res;
        MakeArray("types", res);
        return res;
    }
}

void RegisterTemplatesRpcHandlers(TRPCTemplatesHandler& handler, WBMQTT::PMqttRpcServer rpcServer)
{
    rpcServer->RegisterMethod("templates",
                              "Upload",
                              std::bind(&TRPCTemplatesHandler::UploadTemplate, &handler, std::placeholders::_1));
    rpcServer->RegisterMethod("templates",
                              "Delete",
                              std::bind(&TRPCTemplatesHandler::DeleteTemplate, &handler, std::placeholders::_1));
}

#endif
