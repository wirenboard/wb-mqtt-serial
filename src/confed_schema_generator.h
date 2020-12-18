#include "serial_config.h"


std::string MakeSetupRegisterParameterName(size_t registerIndex);
Json::Value GetDefaultSetupRegisterValue(const Json::Value& setupRegisterSchema);

void MakeSchemaForConfed(Json::Value& configSchema, TTemplateMap& templates);
