#include "serial_config.h"

std::string GetDeviceKey(const std::string& deviceType);
std::string GetSubdeviceSchemaKey(const std::string& subDeviceType);
std::string GetSubdeviceKey(const std::string& subDeviceType);
std::string GetTranslationHash(const std::string& deviceType, const std::string& msg);
void AddTranslations(Json::Value& translations, const Json::Value& deviceSchema);

bool IsRequiredSetupRegister(const Json::Value& setupRegister);
void AddUnitTypes(Json::Value& schema);
Json::Value GetDefaultSetupRegisterValue(const Json::Value& setupRegisterSchema);

//  {
//      "type": "string",
//      "enum": [ VALUE ],
//      "default": VALUE,
//      "options": { "hidden": true }
//  }
Json::Value MakeHiddenProperty(const std::string& value);

//  {
//      "properties": {
//          "protocol": {
//              "type": "string",
//              "options": {
//                  "hidden": true
//              }
//          }
//      }
//  }
Json::Value MakeProtocolProperty();

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

Json::Value GenerateSchemaForConfed(TDeviceTemplate& deviceTemplate,
                                    TSerialDeviceFactory& deviceFactory,
                                    const Json::Value& commonDeviceSchema);
