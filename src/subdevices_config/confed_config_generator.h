#pragma once

#include <wblib/json_utils.h>

namespace Subdevices
{
    void MakeDeviceConfigFromConfed(Json::Value& device,
                                    const std::string& deviceType,
                                    const Json::Value& deviceTemplate);
}
