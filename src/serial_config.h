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
        TTemplateMap(const std::string& templatesDir, const Json::Value& templateSchema);

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

const int DEFAULT_INTER_DEVICE_DELAY_MS = 100;
const int DEFAULT_ACCESS_LEVEL          = 1;
const int DEFAULT_DEVICE_TIMEOUT_MS     = 3000;
const int DEFAULT_DEVICE_FAIL_CYCLES    = 2;

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

    /*! Delay before first request to device. 
    *   This garantees fixed silence period on bus to help device to find start frame condition
    * */
    std::chrono::milliseconds           FirstRequestDelay      = std::chrono::milliseconds(DEFAULT_INTER_DEVICE_DELAY_MS);

    //! Maximum allowed time from request to response.
    std::chrono::milliseconds           ResponseTimeout        = std::chrono::milliseconds(-1);

    //! The period of unsuccessful requests after which the device is considered disconected.
    std::chrono::milliseconds           DeviceTimeout          = std::chrono::milliseconds(DEFAULT_DEVICE_TIMEOUT_MS);

    //! Delay before sending any request
    std::chrono::microseconds           RequestDelay           = std::chrono::microseconds(0);

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
    std::chrono::milliseconds  PollInterval          = std::chrono::milliseconds(20);
    std::chrono::microseconds  RequestDelay          = std::chrono::microseconds(0);

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
