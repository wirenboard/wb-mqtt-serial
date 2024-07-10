#include "confed_device_schemas_map.h"
#include "confed_schema_generator.h"

TDevicesConfedSchemasMap::TDevicesConfedSchemasMap(TTemplateMap& templatesMap,
                                                   TSerialDeviceFactory& deviceFactory,
                                                   const Json::Value& commonDeviceSchema)
    : TemplatesMap(templatesMap),
      CommonDeviceSchema(commonDeviceSchema),
      DeviceFactory(deviceFactory)
{}

std::shared_ptr<Json::Value> TDevicesConfedSchemasMap::GetSchema(const std::string& deviceType)
{
    std::unique_lock lock(Mutex);
    try {
        return Schemas.at(deviceType);
    } catch (const std::out_of_range&) {
        auto deviceTemplate = TemplatesMap.GetTemplate(deviceType);
        auto schema =
            std::make_shared<Json::Value>(GenerateSchemaForConfed(*deviceTemplate, DeviceFactory, CommonDeviceSchema));
        Schemas.emplace(deviceType, schema);
        return schema;
    }
}

void TDevicesConfedSchemasMap::InvalidateCache(const std::string& deviceType)
{
    std::unique_lock lock(Mutex);
    Schemas.erase(deviceType);
}
