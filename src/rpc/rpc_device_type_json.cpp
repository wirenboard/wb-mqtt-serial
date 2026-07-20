#include "rpc_device_type_json.h"
#include "json_common.h"

const std::string CUSTOM_GROUP_NAME = "g-custom";
const std::string WB_GROUP_NAME = "g-wb";
const std::string WB_OLD_GROUP_NAME = "g-wb-old";

Json::Value MakeDeviceTypeJson(const PDeviceTemplate& dt, const std::string& lang)
{
    Json::Value res;
    res["name"] = dt->GetTitle(lang);
    res["deprecated"] = dt->IsDeprecated();
    res["type"] = dt->Type;
    res["protocol"] = dt->GetProtocol();
    res["mqtt-id"] = dt->GetMqttId();
    res["with-subdevices"] = dt->WithSubdevices();
    if (dt->IsUserDefined()) {
        res["user-defined"] = true;
    }
    if (!dt->GetHardware().empty()) {
        auto& hwJsonArray = MakeArray("hw", res);
        for (const auto& hw: dt->GetHardware()) {
            Json::Value hwJson;
            hwJson["signature"] = hw.Signature;
            if (!hw.Fw.empty()) {
                hwJson["fw"] = hw.Fw;
            }
            hwJsonArray.append(hwJson);
        }
    }
    return res;
}

std::string GetGroupTranslation(const std::string& group, const std::string& lang, const Json::Value& groupTranslations)
{
    auto res = groupTranslations.get(lang, Json::Value(Json::objectValue)).get(group, "").asString();
    return res.empty() ? group : res;
}
