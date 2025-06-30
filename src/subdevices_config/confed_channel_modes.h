#pragma once

#include <wblib/json_utils.h>

namespace Subdevices
{
    Json::Value ConfigToHomeuiSubdeviceChannel(const Json::Value& channel);
    Json::Value HomeuiToConfigChannel(const Json::Value& channel);
}
