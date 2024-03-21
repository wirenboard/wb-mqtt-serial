#include "confed_schemas_map.h"

#include <filesystem>

#include "file_utils.h"
#include "json_common.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[templates] "

TDevicesConfedSchemasMap::TDevicesConfedSchemasMap(TTemplateMap& templatesMap, const std::string& schemasFolder)
    : TemplatesMap(templatesMap),
      SchemasFolder(schemasFolder)
{}

const Json::Value& TDevicesConfedSchemasMap::GetSchema(const std::string& deviceType)
{
    try {
        return Schemas.at(deviceType);
    } catch (const std::out_of_range&) {
        try {
            Schemas.emplace(deviceType,
                            WBMQTT::JSON::Parse(
                                GetSchemaFilePath(SchemasFolder, TemplatesMap.GetTemplate(deviceType).GetFilePath())));
            return Schemas.at(deviceType);
        } catch (const std::out_of_range&) {
            throw std::out_of_range("Can't find json-schema for " + deviceType);
        }
    }
}

std::string GetSchemaFilePath(const std::string& schemasFolder, const std::string& templateFilePath)
{
    std::filesystem::path templatePath(templateFilePath);
    return schemasFolder + "/" + templatePath.stem().string() + ".schema.json";
}

TProtocolConfedSchema::TProtocolConfedSchema(const std::string& type,
                                             const std::unordered_map<std::string, std::string>& titleTranslations,
                                             const std::string& filePath)
    : Type(type),
      Title(titleTranslations),
      FilePath(filePath)
{}

std::string TProtocolConfedSchema::GetTitle(const std::string& lang) const
{
    auto it = Title.find(lang);
    if (it != Title.end()) {
        return it->second;
    }
    if (lang != "en") {
        it = Title.find("en");
        if (it != Title.end()) {
            return it->second;
        }
    }
    return Type;
}

const Json::Value& TProtocolConfedSchema::GetSchema()
{
    if (Schema.isNull()) {
        Schema = WBMQTT::JSON::Parse(FilePath);
    }
    return Schema;
}

TProtocolConfedSchemasMap::TProtocolConfedSchemasMap(const std::string& protocolTemplatesFolder,
                                                     const std::string& schemasFolder)
{
    std::vector<Json::Value> res(Json::arrayValue);
    IterateDir(protocolTemplatesFolder, [&](const std::string& name) {
        if (name.find(".schema.json") != std::string::npos) {
            try {
                auto filePath = protocolTemplatesFolder + "/" + name;
                auto schema = WBMQTT::JSON::Parse(filePath);
                std::string type = schema["properties"]["protocol"]["enum"][0].asString();
                std::string title = schema.get("title", type).asString();
                TProtocolConfedSchema pr(type, GetTranslations(title, schema), schemasFolder + "/" + name);
                Schemas.insert({type, pr});
            } catch (const std::exception& e) {
                LOG(Error) << "Failed to parse " << name << "\n" << e.what();
            }
        }
        return false;
    });
}

std::unordered_map<std::string, TProtocolConfedSchema>& TProtocolConfedSchemasMap::GetSchemas()
{
    return Schemas;
}
