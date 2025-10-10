#include "json_common.h"

Json::Value& MakeArray(const std::string& key, Json::Value& node)
{
    return (node[key] = Json::Value(Json::arrayValue));
}

Json::Value& Append(Json::Value& array)
{
    return array.append(Json::Value());
}

Json::Value MakeObject(const std::string& key, const std::string& value)
{
    Json::Value res;
    res[key] = value;
    return res;
}

Json::Value MakeSingleValueProperty(const std::string& value)
{
    Json::Value res;
    res["type"] = "string";
    MakeArray("enum", res).append(value);
    return res;
}

std::unordered_map<std::string, std::string> GetTranslations(const std::string& key, const Json::Value& schema)
{
    std::unordered_map<std::string, std::string> res;
    if (key.empty()) {
        return res;
    }
    if (schema.isMember("translations")) {
        const auto& translations = schema["translations"];
        for (Json::ValueConstIterator it = translations.begin(); it != translations.end(); ++it) {
            std::string tr = it->get(key, std::string()).asString();
            if (!tr.empty()) {
                res.emplace(it.name(), tr);
            }
        }
    }
    if (!res.count("en")) {
        res.emplace("en", key);
    }
    return res;
}

void AppendParams(Json::Value& dst, const Json::Value& src)
{
    for (auto it = src.begin(); it != src.end(); ++it) {
        dst[it.name()] = *it;
    }
}
