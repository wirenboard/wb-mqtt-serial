#pragma once

#include <unordered_map>
#include <wblib/json/json.h>

// key : [ ]
Json::Value& MakeArray(const std::string& key, Json::Value& node);

//  {
//      "key": "value"
//  }
Json::Value MakeObject(const std::string& key, const std::string& value);

//! Appends an empty Json::Value object to array an returns reference to the object
Json::Value& Append(Json::Value& array);

//  {
//      "type": "string",
//      "enum": [ value ]
//  }
Json::Value MakeSingleValueProperty(const std::string& value);

std::unordered_map<std::string, std::string> GetTranslations(const std::string& key, const Json::Value& deviceTemplate);

void AppendParams(Json::Value& dst, const Json::Value& src);
