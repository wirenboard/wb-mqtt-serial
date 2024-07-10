#pragma once

#include <unordered_map>
#include <wblib/json/json.h>

struct TProtocolConfedSchema
{
    std::string Type;

    TProtocolConfedSchema(const std::string& type,
                          const std::unordered_map<std::string, std::string>& titleTranslations,
                          const std::string& filePath);

    std::string GetTitle(const std::string& lang = std::string("en")) const;
    const std::string& GetFilePath() const;

private:
    std::unordered_map<std::string, std::string> Title;
    std::string FilePath;
};

class TProtocolConfedSchemasMap
{
    //! Protocol to TProtocolConfedSchema map
    std::unordered_map<std::string, TProtocolConfedSchema> Schemas;

    //! Protocol to JSON-Schema map
    std::unordered_map<std::string, Json::Value> JsonSchemas;

    const Json::Value& CommonDeviceSchema;

public:
    TProtocolConfedSchemasMap(const std::string& protocolTemplatesFolder, const Json::Value& commonDeviceSchema);

    void AddFolder(const std::string& protocolTemplatesFolder);

    const std::unordered_map<std::string, TProtocolConfedSchema>& GetSchemas() const;

    const Json::Value& GetSchema(const std::string& protocol);
};
