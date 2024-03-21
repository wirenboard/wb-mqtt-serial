#pragma once

#include "templates_map.h"

class TDevicesConfedSchemasMap
{
    TTemplateMap& TemplatesMap;
    std::string SchemasFolder;

    //! Device type to schema map
    std::unordered_map<std::string, Json::Value> Schemas;

public:
    TDevicesConfedSchemasMap(TTemplateMap& templatesMap, const std::string& schemasFolder);

    const Json::Value& GetSchema(const std::string& deviceType);

    void TemplatesHaveBeenChanged();
};

struct TProtocolConfedSchema
{
    std::string Type;

    TProtocolConfedSchema(const std::string& type,
                          const std::unordered_map<std::string, std::string>& titleTranslations,
                          const std::string& filePath);

    std::string GetTitle(const std::string& lang = std::string("en")) const;
    const Json::Value& GetSchema();

private:
    std::unordered_map<std::string, std::string> Title;
    std::string FilePath;
    Json::Value Schema;
};

class TProtocolConfedSchemasMap
{
    //! Protocol to TProtocolConfedSchema map
    std::unordered_map<std::string, TProtocolConfedSchema> Schemas;

public:
    TProtocolConfedSchemasMap(const std::string& protocolTemplatesFolder, const std::string& schemasFolder);

    std::unordered_map<std::string, TProtocolConfedSchema>& GetSchemas();
};

std::string GetSchemaFilePath(const std::string& schemasFolder, const std::string& templateFilePath);
