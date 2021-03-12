#pragma once

#include "serial_config.h"

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData, const std::string& deviceType, Json::Value deviceTemplate);