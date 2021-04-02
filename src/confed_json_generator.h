#include "serial_config.h"

Json::Value MakeJsonForConfed(const std::string&    configFileName,
                              const Json::Value&    baseConfigSchema,
                              TTemplateMap&         templates,
                              TSerialDeviceFactory& deviceFactory);
