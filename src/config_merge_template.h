#pragma once

#include "serial_config.h"

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData, ITemplateMap& templates);