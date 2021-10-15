#include "serial_config.h"

struct TConfigValidationOptions
{
    std::unordered_set<std::string> Protocols;
    std::unordered_set<std::string> DeviceTypes;
};

TConfigValidationOptions GetValidationDeviceTypes(const Json::Value& config);

Json::Value MakeSchemaForConfigValidation(const Json::Value& baseConfigSchema,
                                          const TConfigValidationOptions& options,
                                          TTemplateMap& templates,
                                          TSerialDeviceFactory& deviceFactory);
