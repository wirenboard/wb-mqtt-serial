#include "serial_config.h"

void ValidateConfig(const Json::Value& config,
                    TSerialDeviceFactory& deviceFactory,
                    const Json::Value& baseConfigSchema,
                    TTemplateMap& templates);
