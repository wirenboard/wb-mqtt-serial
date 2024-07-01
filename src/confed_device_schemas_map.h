#pragma once

#include "serial_config.h"
#include "templates_map.h"

class TDevicesConfedSchemasMap
{
    TTemplateMap& TemplatesMap;
    const Json::Value& CommonDeviceSchema;
    TSerialDeviceFactory& DeviceFactory;

    //! Device type to schema map
    std::unordered_map<std::string, std::shared_ptr<Json::Value>> Schemas;

    std::mutex Mutex;

public:
    TDevicesConfedSchemasMap(TTemplateMap& templatesMap,
                             TSerialDeviceFactory& deviceFactory,
                             const Json::Value& commonDeviceSchema);

    /**
     * @brief Get the json-schema for specified device type
     *
     * @throws std::out_of_range if nothing found
     */
    std::shared_ptr<Json::Value> GetSchema(const std::string& deviceType);

    void InvalidateCache(const std::string& deviceType);
};
