#pragma once

#include "serial_config.h"

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData,
                                          const std::string& deviceType,
                                          const Json::Value& deviceTemplate);