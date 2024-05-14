#pragma once

#include <mutex>
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
                    const std::string& protocol,
                    std::shared_ptr<WBMQTT::JSON::TValidator> validator,
                    const std::string& filePath);

    void SetDeprecated();
    void SetWithSubdevices();
    void SetGroup(const std::string& group);
    void SetTitle(const std::unordered_map<std::string, std::string>& translations);
    void SetHardware(const std::vector<TDeviceTemplateHardware>& hardware);
    void SetMqttId(const std::string& id);

    std::string GetTitle(const std::string& lang = std::string("en")) const;
    const Json::Value& GetTemplate();
    const std::string& GetGroup() const;
    const std::vector<TDeviceTemplateHardware>& GetHardware() const;
    const std::string& GetFilePath() const;
    bool IsDeprecated() const;
    bool WithSubdevices() const;
    const std::string& GetProtocol() const;
    const std::string& GetMqttId() const;

private:
    std::unordered_map<std::string, std::string> Title;
    std::string Group;
    bool Deprecated;
    std::vector<TDeviceTemplateHardware> Hardware;
    std::shared_ptr<WBMQTT::JSON::TValidator> Validator;
    std::string FilePath;
    Json::Value Template;
    bool Subdevices;
    std::string Protocol;
    std::string MqttId;
};

typedef std::shared_ptr<TDeviceTemplate> PDeviceTemplate;

class TTemplateMap
{
    /**
     *  @brief Device type to list of TDeviceTemplate's mapping.
     *         Last element is the preferred template
     */
    std::unordered_map<std::string, std::vector<PDeviceTemplate>> Templates;

    std::shared_ptr<WBMQTT::JSON::TValidator> Validator;

    std::mutex Mutex;

    std::string PreferredTemplatesDir;

    PDeviceTemplate MakeTemplateFromJson(const Json::Value& data, const std::string& filePath);
    std::string DeleteTemplateUnsafe(const std::string& path);

public:
    TTemplateMap() = default;

    /**
     * @brief Construct a new TTemplateMap object.
     *
     * @param templateSchema JSON Schema for template file validation
     */
    TTemplateMap(const Json::Value& templateSchema);

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

    /**
     * @brief Notify about modification of template
     *
     * @param filePath path to a modified file
     * @return std::vector<std::string> Array of updated device types
     */
    std::vector<std::string> UpdateTemplate(const std::string& filePath);

    /**
     * @brief Notify about deletion of template file
     *
     * @param filePath path to deleted file
     * @return std::string device type of deleted template
     */
    std::string DeleteTemplate(const std::string& filePath);

    /**
     * @brief Get the device template for requested device type
     *
     * @throws std::out_of_range if nothing found
     */
    PDeviceTemplate GetTemplate(const std::string& deviceType);

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
