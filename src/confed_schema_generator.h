#include "serial_config.h"

const std::string CUSTOM_DEVICE_TYPE = "Custom device";

std::string GetDeviceKey(const std::string& deviceType);
std::string GetSubdeviceSchemaKey(const std::string& deviceType, const std::string& subDeviceType);
std::string GetSubdeviceKey(const std::string& subDeviceType);

bool IsMandatorySetupRegister(const Json::Value& setupRegister);

Json::Value GetDefaultSetupRegisterValue(const Json::Value& setupRegisterSchema);

Json::Value MakeSchemaForConfed(const Json::Value& configSchema, TTemplateMap& templates, TSerialDeviceFactory& deviceFactory);
