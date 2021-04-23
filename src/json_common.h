#include <jsoncpp/json/json.h>

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
Json::Value MakeSingleValuePropery(const std::string& value);
