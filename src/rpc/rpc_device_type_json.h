#pragma once
#include <wblib/json_utils.h>

#include "templates_map.h"

extern const std::string CUSTOM_GROUP_NAME;
extern const std::string WB_GROUP_NAME;
extern const std::string WB_OLD_GROUP_NAME;

Json::Value MakeDeviceTypeJson(const PDeviceTemplate& dt, const std::string& lang);

std::string GetGroupTranslation(const std::string& group,
                                const std::string& lang,
                                const Json::Value& groupTranslations);
