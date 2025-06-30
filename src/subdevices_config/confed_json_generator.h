#pragma once

#include "templates_map.h"

namespace Subdevices
{
    Json::Value MakeDeviceJsonForConfed(const Json::Value& deviceConfig, TDeviceTemplate& deviceTemplate);
}
