#include "confed_schema_generator.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace WBMQTT::JSON;

std::string GetHashedParam(const std::string& deviceType, const std::string& prefix)
{
    return prefix + "_" + std::to_string(std::hash<std::string>()(deviceType));
}

std::string GetSubdeviceSchemaKey(const std::string& deviceType, const std::string& subDeviceType)
{
    return GetDeviceKey(deviceType) + GetHashedParam(subDeviceType, "d");
}

std::string GetDeviceKey(const std::string& deviceType)
{
    return GetHashedParam(deviceType, "s");
}

std::string GetSubdeviceKey(const std::string& subDeviceType)
{
    return GetDeviceKey(subDeviceType) + "_e";
}

std::string MakeSetupRegisterParameterName(size_t registerIndex)
{
    return "set" + std::to_string(registerIndex);
}

Json::Value GetDefaultSetupRegisterValue(const Json::Value& setupRegisterSchema)
{
    if (setupRegisterSchema.isMember("default")) {
        return setupRegisterSchema["default"];
    }
    if (setupRegisterSchema.isMember("enum")) {
        return setupRegisterSchema["enum"][0];
    }
    if (setupRegisterSchema.isMember("min")) {
        return setupRegisterSchema["min"];
    }
    if (setupRegisterSchema.isMember("max")) {
        return setupRegisterSchema["max"];
    }
    return 0;
}

//  {
//      "type": "string",
//      "enum": [ VALUE ],
//      "default": VALUE
//      "options": { "hidden": true }
//  }
Json::Value MakeHiddenPropery(const std::string& value)
{
    Json::Value res;
    res["type"] = "string";
    res["default"] = value;
    res["enum"] = Json::Value(Json::arrayValue);
    res["enum"].append(value);
    res["options"]["hidden"] = true;
    return res;
}

//  {
//      "properties": {
//          "name": {
//              "type": "string",
//              "enum": [ NAME ],
//              "default": NAME,
//              "options": { "hidden": true }
//          }
//      },
//      "required": ["name"]
//  }
Json::Value MakeHiddenNameObject(const std::string& name)
{
    Json::Value r;
    r["properties"]["name"] = MakeHiddenPropery(name);
    r["required"] = Json::Value(Json::arrayValue);
    r["required"].append("name");
    return r;
}

//  {
//      "type": "integer",
//      "title": TITLE,
//      "default": DEFAULT,
//      "minimum": MIN,
//      "maximum": MAX,
//      "enum": [ ... ],
//      "options": {
//          "enumTitles" : [ ... ]
//      },
//      "propertyOrder": INDEX
//  }
Json::Value MakeSetupRegisterSchema(const Json::Value& setupRegister, int index)
{
    Json::Value r;
    r["type"] = "integer";
    r["title"] = setupRegister["title"];
    r["default"] = GetDefaultSetupRegisterValue(setupRegister);
    SetIfExists(r, "enum",    setupRegister, "enum");
    SetIfExists(r, "minimum", setupRegister, "min");
    SetIfExists(r, "maximum", setupRegister, "max");
    if (setupRegister.isMember("enum_titles")) {
        r["options"]["enum_titles"] = setupRegister["enum_titles"];
    }
    r["propertyOrder"] = index;
    return r;
}

//  {
//      "$ref": "#/definitions/channelSettings",
//      "headerTemplate": CHANNEL_NAME,
//      "default": {
//        "name": CHANNEL_NAME,
//        "enabled": CHANNEL_ENABLED,
//        "poll_interval": CHANNEL_POLL_INTERVAL
//      }
//  }
Json::Value MakeTabSimpleChannelSchema(const Json::Value& channelTemplate)
{
    Json::Value r;
    r["$ref"] = "#/definitions/channelSettings";
    r["headerTemplate"] = channelTemplate["name"].asString();
    r["default"]["name"] = channelTemplate["name"].asString();
    SetIfExists(r["default"], "enabled", channelTemplate, "enabled");
    SetIfExists(r["default"], "poll_interval", channelTemplate, "poll_interval");
    return r;
}

//  {
//      "headerTemplate": CHANNEL_NAME,
//      "options": {
//          "keep_oneof_values": false
//      },
//      "allOf":[
//          {
//              "oneOf": [
//                  { "$ref": "#/definitions/DEVICE_SCHEMA_NAME" },
//                  ...
//              ]
//          },
//          {
//              "properties": {
//                  "name": {
//                      "type": "string",
//                      "enum": [ CHANNEL_NAME ],
//                      "default": CHANNEL_NAME,
//                      "options": { "hidden": true }
//                  }
//              },
//              "required": ["name"]
//          }
//      ]
//  }
Json::Value MakeTabOneOfChannelSchema(const Json::Value& channel, const std::string& deviceDefinitionPrefix)
{
    Json::Value r;
    r["headerTemplate"] = channel["name"].asString();
    r["options"]["keep_oneof_values"] = false;
    r["allOf"] = Json::Value(Json::arrayValue);

    Json::Value item;
    Json::Value ar(Json::arrayValue);
    for (const auto& deviceName: channel["oneOf"]) {
        Json::Value ref;
        // TODO: move to common function
        ref["$ref"] = "#/definitions/" + deviceDefinitionPrefix + GetHashedParam(deviceName.asString(), "d");
        ar.append(ref);
    }
    item["oneOf"] = ar;
    r["allOf"].append(item);
    r["allOf"].append(MakeHiddenNameObject(channel["name"].asString()));

    return r;
}

//  {
//      "headerTemplate": CHANNEL_NAME,
//      "allOf": [
//          { "$ref": "#/definitions/DEVICE_SCHEMA_NAME" },
//          {
//              "properties": {
//                  "name": {
//                      "type": "string",
//                      "enum": [ CHANNEL_NAME ],
//                      "default": CHANNEL_NAME,
//                      "options": { "hidden": true }
//                  }
//              },
//              "required": ["name"]
//          }
//      ],
//      "options": {
//          "disable_properties": true
//      }
//  }
Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channel, const std::string& deviceDefinitionPrefix)
{
    Json::Value r;
    r["headerTemplate"] = channel["name"].asString();
    r["options"]["disable_properties"] = true;
    r["allOf"] = Json::Value(Json::arrayValue);
    Json::Value item;
    // TODO: move to common function
    item["$ref"] = "#/definitions/" + deviceDefinitionPrefix + GetHashedParam(channel["device_type"].asString(), "d");
    r["allOf"].append(item);
    r["allOf"].append(MakeHiddenNameObject(channel["name"].asString()));
    return r;
}

Json::Value MakeTabChannelSchema(const Json::Value& channel, const std::string& deviceDefinitionPrefix)
{
    if (channel.isMember("oneOf")) {
        return MakeTabOneOfChannelSchema(channel, deviceDefinitionPrefix);
    }
    if (channel.isMember("device_type")) {
        return MakeTabSingleDeviceChannelSchema(channel, deviceDefinitionPrefix);
    }
    return MakeTabSimpleChannelSchema(channel);
}

//  Schema for tabs
//  {
//      "type": "array",
//      "title": TITLE,
//      "options": {
//          "disable_edit_json": true,
//          "disable_array_reorder": true,
//          "disable_collapse": true
//      },
//      "propertyOrder": PROPERTY_ORDER,
//      "minItems": CHANNELS_COUNT,
//      "maxItems": CHANNELS_COUNT,
//      "items": [
//         CHANNELS_SCHEMAS
//      ],
//      "_format": "tabs"
//  }
//
//  Schema for table
//  {
//      "type": "array",
//      "title": TITLE,
//      "options": {
//          "disable_edit_json": true,
//          "disable_array_reorder": true,
//          "disable_collapse": true,
//          "grid_columns": 12
//      },
//      "propertyOrder": PROPERTY_ORDER,
//      "items": { "$ref", "#/definitions/tableChannelSettings"},
//      "minItems": CHANNELS_COUNT,
//      "maxItems": CHANNELS_COUNT,
//      "default: [ 
//          {
//              "name": CHANNEL1_NAME,
//              "enabled": CHANNEL_ENABLED,
//              "poll_interval": CHANNEL_POLL_INTERVAL
//          },
//          ...
//      ],
//      "_format": "table"
//  }
Json::Value MakeChannelsSchema(const Json::Value& channels,
                               const std::string& deviceDefinitionPrefix,
                               int                propertyOrder,
                               const std::string& format,
                               const std::string& title,
                               bool               allowCollapse = false)
{
    Json::Value r;
    r["type"] = "array";
    r["title"] = title;
    r["options"]["disable_edit_json"] = true;
    r["options"]["disable_array_reorder"] = true;
    if (!allowCollapse) {
        r["options"]["disable_collapse"] = true;
    }
    r["propertyOrder"] = propertyOrder;

    bool tabs = false;

    if (format == "default") {
        for (const auto& channel: channels) {
            if (IsSubdeviceChannel(channel)) {
                tabs = true;
                break;
            }
        }
    } else {
        tabs = true;
    }

    if (tabs) {
        Json::Value items(Json::arrayValue);
        for (const auto& channel: channels) {
            items.append(MakeTabChannelSchema(channel, deviceDefinitionPrefix));
        }
        r["items"] = items;
        r["minItems"] = items.size();
        r["maxItems"] = items.size();
    } else {
        r["options"]["grid_columns"] = 12;
        r["items"]["$ref"] = "#/definitions/tableChannelSettings";
        Json::Value defaults(Json::arrayValue);
        for (const auto& channel: channels) {
            Json::Value v;
            v["name"] = channel["name"];
            SetIfExists(v, "enabled", channel, "enabled");
            SetIfExists(v, "poll_interval", channel, "poll_interval");
            defaults.append(v);
        }
        r["minItems"] = defaults.size();
        r["maxItems"] = defaults.size();
        r["default"] = defaults;
    }
    if ( format != "list" ) {
        r["_format"] = (tabs ? "tabs" : "table");
    }
    return r;
}

//  {
//      "type": "object",
//      "title": DEVICE_TYPE,
//      "required": ["DEVICE_HASH"],
//      "options": {
//          "disable_edit_json": true,
//          "disable_properties": true,
//          "disable_collapse": true
//      },
//      "properties": {
//          "DEVICE_HASH": {
//              "type": "object",
//              "title": " ",
//              "options": {
//                  "disable_edit_json": true,
//                  "disable_properties": true,
//                  "disable_collapse": true
//              },
//              "properties": {
//                  "set_0": { ... },
//                  ...
//                  "standard_channels": STANDARD_CHANNELS_SCHEMA
//              }
//          }
//      }
//  }
Json::Value MakeSubDeviceUISchema(const std::string&      deviceType,
                                  const TDeviceTemplate&  subdeviceTemplate,
                                  const std::string&      devicePrefix)
{
    Json::Value res;
    res["type"] = "object";
    res["title"] = subdeviceTemplate.Title;
    res["options"]["disable_edit_json"] = true;
    res["options"]["disable_properties"] = true;
    res["options"]["disable_collapse"] = true;

    auto set = GetSubdeviceKey(deviceType);

    Json::Value req(Json::arrayValue);
    req.append(set);
    res["required"] = req;

    res["properties"][set]["type"] = "object";
    res["properties"][set]["title"] = " ";
    res["properties"][set]["options"]["disable_edit_json"] = true;
    res["properties"][set]["options"]["disable_properties"] = true;
    res["properties"][set]["options"]["disable_collapse"] = true;

    std::string channelsFormat("default");
    std::string channelsTitle(" ");
    std::string setupFormat("grid");
    if (subdeviceTemplate.Schema.isMember("ui_options")) {
        Get(subdeviceTemplate.Schema["ui_options"], "setup_format", setupFormat);
        Get(subdeviceTemplate.Schema["ui_options"], "channels_format", channelsFormat);
        Get(subdeviceTemplate.Schema["ui_options"], "channels_title", channelsTitle);
    }
    if (setupFormat != "list") {
        res["properties"][set]["_format"] = setupFormat;
    }

    int i = 0;
    if (subdeviceTemplate.Schema.isMember("setup")) {
        for (const auto& setupRegister: subdeviceTemplate.Schema["setup"]) {
            if (!setupRegister.isMember("value")) {
                res["properties"][set]["properties"][MakeSetupRegisterParameterName(i)] = MakeSetupRegisterSchema(setupRegister, i);
                ++i;
            }
        }
    }

    if (subdeviceTemplate.Schema.isMember("channels")) {
        res["properties"][set]["properties"]["standard_channels"] = MakeChannelsSchema(subdeviceTemplate.Schema["channels"],
                                                                                       devicePrefix,
                                                                                       i,
                                                                                       channelsFormat,
                                                                                       channelsTitle);
    } else {
        // No channels, so it is just a stub device with predefined settings, nothig to show in UI.
        res["properties"][set]["options"]["collapsed"] = true;
    }

    return res;
}

//  {
//      "type": "object",
//      "title": "Device options",
//      "properties": {
//          "set_0": { ... },
//          ...
//      },
//      "options": {
//          "disable_edit_json": true,
//          "disable_properties": true
//      },
//      "propertyOrder": PROPERTY_ORDER
//  }
Json::Value MakeDeviceSettingsUI(const Json::Value& deviceTemplate, int propertyOrder)
{
    Json::Value res;

    res["type"] = "object";
    res["title"] = "Device options";
    res["options"]["disable_edit_json"] = true;
    res["options"]["disable_properties"] = true;
    res["propertyOrder"] = propertyOrder;

    size_t i = 0;
    for (const auto& setupRegister: deviceTemplate["setup"]) {
        if (!setupRegister.isMember("value")) {
            res["properties"][MakeSetupRegisterParameterName(i)] = MakeSetupRegisterSchema(setupRegister, i);
            ++i;
        }
    }
    return res;
}

//  {
//      "type": "object",
//      "title": DEVICE_TITLE,
//      "required": ["DEVICE_HASH"],
//      "properties": {
//          "DEVICE_HASH": {
//              "title": " ",
//              "type": "object",
//              "propertyOrder": 9
//              "options": {
//                  "disable_edit_json": true,
//              },
//              "allOf": [
//                  { "$ref": "#/definitions/deviceProperties" },
//                  { "$ref": "#/definitions/deviceConfigParamsForUI" },
//                  { "$ref": "#/definitions/customDevice" }    // for custom device only
//              ],
//              "properties": {
//                  "standard_channels": STANDARD_CHANNELS_SCHEMA,
//                  "standard_setup": STANDARD_SETUP_SCHEMA,
//                  "slave_id": {
//                      "$ref": "#/definitions/slave_id",
//                      "options": { "keep_oneof_values": false }    // we must to place it here due to a bug in json-editor
//                  } 
//                  or
//                  "slave_id": {
//                      "$ref": "#/definitions/slave_id_broadcast",
//                      "options": { "keep_oneof_values": false }    // we must to place it here due to a bug in json-editor
//                  }
//              },
//              "defaultProperties": ["slave_id", "standard_channels", "standard_setup"],
//              "required": ["slave_id"]
//          },
//          "_format": SETUP_FORMAT
//      }
//  }
std::pair<Json::Value, Json::Value> MakeDeviceUISchema(const TDeviceTemplate& deviceTemplate, TSerialDeviceFactory& deviceFactory)
{
    auto set = GetDeviceKey(deviceTemplate.Type);

    Json::Value res;
    res["type"] = "object";
    res["title"] = deviceTemplate.Title;

    Json::Value req(Json::arrayValue);
    req.append(set);
    res["required"] = req;

    Json::Value& pr = res["properties"][set];

    pr["type"] = "object";
    pr["title"] = " ";
    pr["propertyOrder"] = 9;
    pr["options"]["disable_edit_json"] = true;

    Json::Value ar(Json::arrayValue);
    Json::Value ref;
    ref["$ref"] = "#/definitions/deviceProperties";
    ar.append(ref);
    ref["$ref"] = "#/definitions/deviceConfigParamsForUI";
    ar.append(ref);
    if (deviceTemplate.Type == CUSTOM_DEVICE_TYPE) {
        ref["$ref"] = "#/definitions/customDevice";
        ar.append(ref);
    }
    res["properties"][set]["allOf"] = ar;

    std::string channelsFormat("default");
    std::string channelsTitle("Channels");
    if (deviceTemplate.Schema.isMember("ui_options")) {
        SetIfExists(pr, "_format", deviceTemplate.Schema["ui_options"], "setup_format");
        Get(deviceTemplate.Schema["ui_options"], "channels_format", channelsFormat);
        Get(deviceTemplate.Schema["ui_options"], "channels_title", channelsTitle);
    }

    if (deviceTemplate.Schema.isMember("channels")) {
        pr["properties"]["standard_channels"] = MakeChannelsSchema(deviceTemplate.Schema["channels"], set, 98, channelsFormat, channelsTitle, true);
    }

    if (deviceTemplate.Schema.isMember("setup")) {
        pr["properties"]["standard_setup"] = MakeDeviceSettingsUI(deviceTemplate.Schema, 97);
    }

    if ((deviceTemplate.Type != CUSTOM_DEVICE_TYPE) && deviceFactory.GetProtocol(GetProtocolName(deviceTemplate.Schema))->SupportsBroadcast()) {
        pr["properties"]["slave_id"]["$ref"] = "#/definitions/slave_id_broadcast";
    } else {
        pr["properties"]["slave_id"]["$ref"] = "#/definitions/slave_id";
    }
    pr["properties"]["slave_id"]["options"]["keep_oneof_values"] = false;

    req.clear();
    req.append("slave_id");
    pr["required"] = req;

    if (deviceTemplate.Schema.isMember("channels")) {
        req.append("standard_channels");
    }
    if (deviceTemplate.Schema.isMember("setup")) {
        req.append("standard_setup");
    }
    pr["defaultProperties"] = req;

    TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, deviceTemplate.Schema);
    Json::Value definitions;
    if (deviceTemplate.Schema.isMember("subdevices")) {
        for (const auto& subDevice: deviceTemplate.Schema["subdevices"]) {
            auto name = subDevice["device_type"].asString();
            definitions[GetSubdeviceSchemaKey(deviceTemplate.Type, name)] = MakeSubDeviceUISchema(name, subdeviceTemplates.GetTemplate(name), set);
        }
    }

    return std::make_pair(res, definitions);
}

void AppendDeviceSchemas(Json::Value& list, Json::Value& definitions, TTemplateMap& templates, TSerialDeviceFactory& deviceFactory)
{
    for (const auto& t: templates.GetTemplatesOrderedByName()) {
        try {
            auto s = MakeDeviceUISchema(*t, deviceFactory);
            list.append(s.first);
            AppendParams(definitions, s.second);
        } catch (const std::exception& e) {
            LOG(Error) << e.what();
        }
    }
}

void MakeSchemaForConfed(Json::Value& configSchema, TTemplateMap& templates, TSerialDeviceFactory& deviceFactory)
{
    // Let's replace #/definitions/device/oneOf by list of device's generated from templates
    if (configSchema["definitions"]["device"].isMember("oneOf")) {
        Json::Value newArray(Json::arrayValue);
        AppendDeviceSchemas(newArray, configSchema["definitions"], templates, deviceFactory);
        configSchema["definitions"]["device"]["oneOf"] = newArray;
    }
    configSchema.setComment(std::string("// THIS FILE IS GENERATED BY wb-mqtt-serial SERVICE. DO NOT EDIT IT!"), Json::commentBefore);
}
