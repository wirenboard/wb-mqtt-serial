#pragma once

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <exception>
#include <map>

#include "register.h"

#if defined(__APPLE__) || defined(__APPLE_CC__)
#   include <json/json.h>
#else
#   include <jsoncpp/json/json.h>
#endif

#include <wblib/json_utils.h>

#include "port.h"
#include "serial_device.h"

class ITemplateMap
{
    public:
        virtual ~ITemplateMap() = default;

        virtual const Json::Value& GetTemplate(const std::string& deviceType) = 0;
};

class TTemplateMap: public ITemplateMap
{
        std::unordered_map<std::string, std::string> TemplateFiles;
        std::map<std::string, Json::Value>           ValidTemplates;
        std::unique_ptr<WBMQTT::JSON::TValidator>    Validator;
    public:
        TTemplateMap() = default;

        /**
         * @brief Construct a new TTemplateMap object.
         *        Throws TConfigParserException if can't open templatesDir.
         * 
         * @param templatesDirs directory with templates
         * @param templateSchema JSON Schema for template file validation
         */
        TTemplateMap(const std::string& templatesDir, const Json::Value& templateSchema);

        /**
         * @brief Add templates from templatesDir to map.
         *        Throws TConfigParserException if can't open templatesDir.
         * 
         * @param templatesDir directory with templates
         */
        void AddTemplatesDir(const std::string& templatesDir);

        const Json::Value& GetTemplate(const std::string& deviceType) override;

        const std::map<std::string, Json::Value>& GetTemplates();
};

struct TPortConfig 
{
    PPort                      Port;
    int                        MaxUnchangedInterval;
    std::vector<PDeviceConfig> DeviceConfigs;
    std::chrono::milliseconds  PollInterval          = DefaultPollInterval;
    std::chrono::microseconds  RequestDelay          = std::chrono::microseconds::zero();

    /**
     * @brief Maximum allowed time from request to response for any device connected to the port.
     * -1 if not set, DefaultResponseTimeout will be used.
     * The timeout is used if device's ResponseTimeout is not set or if device's ResponseTimeout is smaller.
     */
    std::chrono::milliseconds  ResponseTimeout = std::chrono::milliseconds(-1);

    bool IsModbusTcp = false;

    void AddDeviceConfig(PDeviceConfig device_config, TSerialDeviceFactory& deviceFactory);
};

typedef std::shared_ptr<TPortConfig> PPortConfig;

struct THandlerConfig 
{
    bool                     Debug                = false;
    int                      MaxUnchangedInterval = -1;
    std::vector<PPortConfig> PortConfigs;

    void AddPortConfig(PPortConfig portConfig);
};

typedef std::shared_ptr<THandlerConfig> PHandlerConfig;

class TConfigParserException: public std::runtime_error {
public:
    TConfigParserException(const std::string& message);
};

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& configSchema);
void AddProtocolType(Json::Value& configSchema, const std::string& protocolType);
void AddRegisterType(Json::Value& configSchema, const std::string& registerType);

typedef std::function<std::pair<PPort, bool>(const Json::Value& config)> TPortFactoryFn;
std::pair<PPort, bool> DefaultPortFactory(const Json::Value& port_data);

Json::Value LoadConfigSchema(const std::string& schemaFileName);

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& configSchema,
                          TTemplateMap& templates,
                          TPortFactoryFn portFactory = DefaultPortFactory);

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& configSchema,
                          TPortFactoryFn portFactory = DefaultPortFactory);

Json::Value MakeJsonForConfed(const std::string& configFileName, const Json::Value& configSchema, TTemplateMap& templates);
Json::Value MakeConfigFromConfed(std::istream& stream, TTemplateMap& templates);
void MakeSchemaForConfed(Json::Value& schema, TTemplateMap& templates);