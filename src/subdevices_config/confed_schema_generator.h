#pragma once

#include <wblib/json_utils.h>

#include "serial_config.h"
#include "templates_map.h"

namespace Subdevices
{
    std::string GetSubdeviceSchemaKey(const std::string& subDeviceType);
    std::string GetSubdeviceKey(const std::string& subDeviceType);

    /**
     * @brief Creates channels for groups and transforms group declarations into subdevice declarations.
     *        Newly created subdevice declarations will be added to an array.
     *
     * @param schema device template, it will be modified by the function
     * @param subdevices new subdevices will be added to the list
     * @param mainSchemaTranslations pointer to a node with translations of a whole schema
     */
    void TransformGroupsToSubdevices(Json::Value& schema,
                                     Json::Value& subdevices,
                                     Json::Value* mainSchemaTranslations = nullptr);

    Json::Value MakeDeviceSchemaForConfed(TDeviceTemplate& deviceTemplate,
                                          TSerialDeviceFactory& deviceFactory,
                                          const Json::Value& commonDeviceSchema);
}
