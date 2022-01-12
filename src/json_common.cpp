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
