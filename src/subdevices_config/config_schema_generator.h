#pragma once

#include "confed_protocol_schemas_map.h"
#include "serial_config.h"

namespace Subdevices
{
    Json::Value MakeSchemaForDeviceConfigValidation(const Json::Value& commonDeviceSchema,
                                                    TDeviceTemplate& deviceTemplate,
                                                    TSerialDeviceFactory& deviceFactory);
}
