#pragma once

#include <wblib/json_utils.h>

struct TDeviceTemplateHardware
{
    std::string Signature; //! Device signature
    std::string Fw;        //! Firmware version (semver)
};

struct TDeviceTemplate
{
    std::string Type;

    TDeviceTemplate(const std::string& type,
                    std::shared_ptr<WBMQTT::JSON::TValidator> validator,
                    const std::string& filePath);

    void SetDeprecated();
    void SetWithSubdevices();
    void SetGroup(const std::string& group);
    void SetTitle(const std::unordered_map<std::string, std::string>& translations);

    std::string GetTitle(const std::string& lang = std::string("en")) const;
    const Json::Value& GetTemplate();
    const std::string& GetGroup() const;
    const std::string& GetFilePath() const;
    bool IsDeprecated() const;
    bool WithSubdevices() const;

private:
    std::unordered_map<std::string, std::string> Title;
    std::string Group;
    bool Deprecated;
    std::vector<TDeviceTemplateHardware> Hardware;
    std::shared_ptr<WBMQTT::JSON::TValidator> Validator;
    std::string FilePath;
    Json::Value Template;
    bool Subdevices;
};

typedef std::shared_ptr<TDeviceTemplate> PDeviceTemplate;

class TTemplateMap
{
    /**
     *  @brief Device type to TDeviceTemplate mapping.
     */
    std::unordered_map<std::string, PDeviceTemplate> Templates;

    std::shared_ptr<WBMQTT::JSON::TValidator> Validator;

    PDeviceTemplate MakeTemplateFromJson(const Json::Value& data, const std::string& filePath);

public:
    TTemplateMap() = default;

    /**
     * @brief Construct a new TTemplateMap object.
     *        Throws TConfigParserException if can't open templatesDir.
     *
     * @param templatesDirs directory with templates
     * @param templateSchema JSON Schema for template file validation
     * @param passInvalidTemplates false - throw exception if a folder contains json without device_type parameter
     *                             true - print log message and continue folder processing
     */
    TTemplateMap(const std::string& templatesDir, const Json::Value& templateSchema, bool passInvalidTemplates = true);

    /**
     * @brief Add templates from templatesDir to map.
     *        Throws TConfigParserException if can't open templatesDir.
     *
     * @param templatesDir directory with templates
     * @param passInvalidTemplates false - throw exception if a folder contains json without device_type parameter
     *                             true - print log message and continue folder processing
     * @param settings Json with jsoncpp parser settings
     */
    void AddTemplatesDir(const std::string& templatesDir,
                         bool passInvalidTemplates = true,
                         const Json::Value& settings = Json::Value());

    TDeviceTemplate& GetTemplate(const std::string& deviceType);

    std::vector<PDeviceTemplate> GetTemplates();
};

struct TSubDeviceTemplate
{
    std::string Type;
    std::string Title;
    Json::Value Schema;
};

class TSubDevicesTemplateMap
{
    std::unordered_map<std::string, TSubDeviceTemplate> Templates;
    std::string DeviceType;

public:
    TSubDevicesTemplateMap(const std::string& deviceType, const Json::Value& device);

    const TSubDeviceTemplate& GetTemplate(const std::string& deviceType);

    std::vector<std::string> GetDeviceTypes() const;

    void AddSubdevices(const Json::Value& subdevicesArray);
};
