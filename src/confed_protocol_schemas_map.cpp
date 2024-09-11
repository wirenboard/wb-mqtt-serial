#include "confed_protocol_schemas_map.h"

#include <filesystem>

#include "confed_schema_generator.h"
#include "file_utils.h"
#include "json_common.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[templates] "

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

const std::string& TProtocolConfedSchema::GetFilePath() const
{
    return FilePath;
}

TProtocolConfedSchemasMap::TProtocolConfedSchemasMap(const std::string& protocolTemplatesFolder,
                                                     const Json::Value& commonDeviceSchema)
    : CommonDeviceSchema(commonDeviceSchema)
{
    AddFolder(protocolTemplatesFolder);
}

void TProtocolConfedSchemasMap::AddFolder(const std::string& protocolTemplatesFolder)
{
    IterateDir(protocolTemplatesFolder, [&](const std::string& name) {
        if (name.find(".schema.json") != std::string::npos) {
            try {
                auto filePath = protocolTemplatesFolder + "/" + name;
                auto schema = WBMQTT::JSON::Parse(filePath);
                std::string type = schema["properties"]["protocol"]["enum"][0].asString();
                std::string title = schema.get("title", type).asString();
                TProtocolConfedSchema pr(type, GetTranslations(title, schema), filePath);
                Schemas.insert({type, pr});
            } catch (const std::exception& e) {
                LOG(Error) << "Failed to parse " << name << "\n" << e.what();
            }
        }
        return false;
    });
}

const std::unordered_map<std::string, TProtocolConfedSchema>& TProtocolConfedSchemasMap::GetSchemas() const
{
    return Schemas;
}

const Json::Value& TProtocolConfedSchemasMap::GetSchema(const std::string& protocol)
{
    try {
        return JsonSchemas.at(protocol);
    } catch (const std::out_of_range&) {
        try {
            auto schema = WBMQTT::JSON::Parse(Schemas.at(protocol).GetFilePath());
            schema["definitions"] = CommonDeviceSchema["definitions"];
            AddTranslations(schema["translations"], CommonDeviceSchema);
            JsonSchemas[protocol].swap(schema);
            return JsonSchemas.at(protocol);
        } catch (const std::out_of_range&) {
            throw std::out_of_range("Can't find schema for " + protocol);
        }
    }
}
