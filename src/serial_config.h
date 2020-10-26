#pragma once

#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <exception>
#include <map>

#include "register.h"
#include "port_settings.h"

#if defined(__APPLE__) || defined(__APPLE_CC__)
#   include <json/json.h>
#else
#   include <jsoncpp/json/json.h>
#endif

#include <wblib/json_utils.h>

class TTemplateMap 
{
        std::unordered_map<std::string, std::string> TemplateFiles;
        std::unordered_map<std::string, Json::Value> ValidTemplates;
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

        const Json::Value& GetTemplate(const std::string& deviceType);
};

struct TDeviceChannelConfig 
{
    std::string                  Name;
    std::string                  Type;
    std::string                  DeviceId; // FIXME
    int                          Order            = 0;
    std::string                  OnValue;
    int                          Max              = -1;
    bool                         ReadOnly         = false;
    std::vector<PRegisterConfig> RegisterConfigs;

    TDeviceChannelConfig(const std::string& name                 = "",
                         const std::string& type                 = "text",
                         const std::string& deviceId             = "",
                         int                order                = 0,
                         const std::string& onValue              = "",
                         int                max                  = - 1,
                         bool               readOnly             = false,
                         const std::vector<PRegisterConfig> regs = std::vector<PRegisterConfig>());
};

typedef std::shared_ptr<TDeviceChannelConfig> PDeviceChannelConfig;

struct TDeviceSetupItemConfig 
{
    std::string     Name;
    PRegisterConfig RegisterConfig;
    int             Value;

    TDeviceSetupItemConfig(const std::string& name, PRegisterConfig reg, int value);
};

typedef std::shared_ptr<TDeviceSetupItemConfig> PDeviceSetupItemConfig;

const int DEFAULT_ACCESS_LEVEL       = 1;
const int DEFAULT_DEVICE_FAIL_CYCLES = 2;

const std::chrono::milliseconds DefaultPollInterval(20);
const std::chrono::milliseconds DefaultResponseTimeout(500);
const std::chrono::milliseconds DefaultDeviceTimeout(3000);

struct TDeviceConfig 
{
    std::string                         Id;
    std::string                         Name;
    std::string                         SlaveId;
    std::string                         DeviceType;
    std::string                         Protocol;
    std::vector<PDeviceChannelConfig>   DeviceChannelConfigs;
    std::vector<PDeviceSetupItemConfig> SetupItemConfigs;
    std::vector<uint8_t>                Password;

    //! Maximum allowed time from request to response. -1 if not set, DefaultResponseTimeout will be used.
    std::chrono::milliseconds           ResponseTimeout        = std::chrono::milliseconds(-1);

    //! Minimum inter-frame delay.
    std::chrono::milliseconds           FrameTimeout           = std::chrono::milliseconds::zero();

    //! The period of unsuccessful requests after which the device is considered disconected.
    std::chrono::milliseconds           DeviceTimeout          = DefaultDeviceTimeout;

    //! Delay before sending any request
    std::chrono::microseconds           RequestDelay           = std::chrono::microseconds::zero();

    int                                 AccessLevel            = DEFAULT_ACCESS_LEVEL;
    int                                 MaxRegHole             = 0;
    int                                 MaxBitHole             = 0;
    int                                 MaxReadRegisters       = 1;
    int                                 Stride                 = 0;
    int                                 Shift                  = 0;
    PRegisterTypeMap                    TypeMap                = 0;
    int                                 DeviceMaxFailCycles    = DEFAULT_DEVICE_FAIL_CYCLES;

    TDeviceConfig(const std::string& name = "", const std::string& slave_id = "", const std::string& protocol = "");

    int NextOrderValue() const;
    void AddChannel(PDeviceChannelConfig channel);
    void AddSetupItem(PDeviceSetupItemConfig item);
};

typedef std::shared_ptr<TDeviceConfig> PDeviceConfig;

struct TPortConfig 
{
    PPortSettings              ConnSettings;
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

    void AddDeviceConfig(PDeviceConfig device_config);
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

typedef std::function<PRegisterTypeMap(PDeviceConfig device_config)> TGetRegisterTypeMapFn;

Json::Value LoadConfigSchema(const std::string& schemaFileName);

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TGetRegisterTypeMapFn getRegisterTypeMapFn,
                          const Json::Value& configSchema,
                          TTemplateMap& templates);

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TGetRegisterTypeMapFn getRegisterTypeMapFn,
                          const Json::Value& configSchema);
