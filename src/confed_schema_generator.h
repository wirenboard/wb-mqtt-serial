#include "serial_config.h"

std::string GetDeviceKey(const std::string& deviceType);
std::string GetSubdeviceSchemaKey(const std::string& deviceType, const std::string& subDeviceType);
std::string GetSubdeviceKey(const std::string& subDeviceType);

bool IsRequiredSetupRegister(const Json::Value& setupRegister);

Json::Value MakeSchemaForConfed(const Json::Value& configSchema,
                                TTemplateMap& templates,
                                TSerialDeviceFactory& deviceFactory);

/**
 * @brief Creates channels for groups and transforms group declarations into subdevice declarations.
 *        Newly created subdevice declarations will be added to an array.
 *
 * @param schema device template, it will be modified by the function
 * @param subdevices new subdevices will be added to the list
 */
void TransformGroupsToSubdevices(Json::Value& schema,
                                 Json::Value& subdevices,
                                 Json::Value* mainSchemaTranslations = nullptr);
