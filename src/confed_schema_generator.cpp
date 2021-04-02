#include "confed_schema_generator.h"
#include "log.h"
#include "json_common.h"

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
    MakeArray("enum", res).append(value);
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
    MakeArray("required", r).append("name");
    return r;
}

bool IsRequiredSetupRegister(const Json::Value& setupRegister)
{
    return setupRegister.get("required", false).asBool();
}

//  {
//      "type": "integer",
//      "title": TITLE,
//      "default": DEFAULT,
//      "minimum": MIN,
//      "maximum": MAX,
//      "enum": [ ... ],
//      "propertyOrder": INDEX
//      "options": {
//          "enumTitles" : [ ... ],
//      },
//  }
Json::Value MakeParameterSchema(const Json::Value& setupRegister, int index)
{
    Json::Value r;
    r["type"] = "integer";
    r["title"] = setupRegister["title"];
    r["default"] = GetDefaultSetupRegisterValue(setupRegister);
    SetIfExists(r, "enum",    setupRegister, "enum");
    SetIfExists(r, "minimum", setupRegister, "min");
    SetIfExists(r, "maximum", setupRegister, "max");
    r["propertyOrder"] = index;
    if (setupRegister.isMember("enum_titles")) {
        r["options"]["enum_titles"] = setupRegister["enum_titles"];
    }
    return r;
}

//  {
//      "allOf": [
//          { "$ref": "#/definitions/channelSettings" },
//          {
//              "properties": {
//                  "name": {
//                      "type": "string",
//                      "enum": [ CHANNEL_NAME ],
//                      "options": {
//                          "hidden": true
//                      }
//                  }
//              },
//              "required": [ "name" ]
//          }
//      ],
//      "headerTemplate": CHANNEL_NAME,
//      "options": {
//          "wb": { "disable_panel": true }
//      },
//      "default": {
//        "name": CHANNEL_NAME,
//        "enabled": CHANNEL_ENABLED,
//        "poll_interval": CHANNEL_POLL_INTERVAL
//      }
//  }
Json::Value MakeTabSimpleChannelSchema(const Json::Value& channelTemplate)
{
    Json::Value r;
    auto& allOf = MakeArray("allOf", r);
    allOf.append(MakeObject("$ref", "#/definitions/channelSettings"));
    allOf.append(MakeHiddenNameObject(channelTemplate["name"].asString()));
    r["headerTemplate"] = channelTemplate["name"].asString();
    r["default"]["name"] = channelTemplate["name"].asString();
    r["options"]["wb"]["disable_panel"] = true;
    SetIfExists(r["default"], "enabled", channelTemplate, "enabled");
    SetIfExists(r["default"], "poll_interval", channelTemplate, "poll_interval");
    return r;
}

//  {
//      "headerTemplate": CHANNEL_NAME,
//      "options": {
//          "keep_oneof_values": false,
//          "wb": {
//              "disable_title": true
//          }
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
Json::Value MakeTabOneOfChannelSchema(const Json::Value& channel, const std::string& deviceType)
{
    Json::Value r;
    r["headerTemplate"] = channel["name"].asString();
    r["options"]["keep_oneof_values"] = false;
    r["options"]["wb"]["disable_title"] = true;
    auto& allOf = MakeArray("allOf", r);

    auto& oneOf = MakeArray("oneOf", Append(allOf));
    for (const auto& subDeviceName: channel["oneOf"]) {
        Append(oneOf)["$ref"] = "#/definitions/" + GetSubdeviceSchemaKey(deviceType, subDeviceName.asString());
    }
    allOf.append(MakeHiddenNameObject(channel["name"].asString()));
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
//          "wb": {
//              "disable_panel": true
//          }
//      }
//  }
Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channel, const std::string& deviceType)
{
    Json::Value r;
    r["headerTemplate"] = channel["name"].asString();
    r["options"]["wb"]["disable_panel"] = true;
    auto& allOf = MakeArray("allOf", r);
    Append(allOf)["$ref"] = "#/definitions/" + GetSubdeviceSchemaKey(deviceType, channel["device_type"].asString());
    allOf.append(MakeHiddenNameObject(channel["name"].asString()));
    return r;
}

Json::Value MakeTabChannelSchema(const Json::Value& channel, const std::string& deviceType)
{
    if (channel.isMember("oneOf")) {
        return MakeTabOneOfChannelSchema(channel, deviceType);
    }
    if (channel.isMember("device_type")) {
        return MakeTabSingleDeviceChannelSchema(channel, deviceType);
    }
    return MakeTabSimpleChannelSchema(channel);
}

//  Schema for tabs
//  {
//      "type": "array",
//      "title": TITLE,                      // if any
//      "options": {
//          "disable_edit_json": true,
//          "disable_array_reorder": true,
//          "disable_collapse": true,
//          "compact": true                  // if no title
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
//  Schema for table or list
//  {
//      "type": "array",
//      "title": TITLE,                      // if any
//      "options": {
//          "disable_edit_json": true,
//          "disable_array_reorder": true,
//          "grid_columns": 12,
//          "compact": true,                  // if no title
//          "wb": {
//              "disable_title": true,
//              "disable_array_item_panel": true
//          }
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
//      "_format": "table"                   // if table
//  }
Json::Value MakeChannelsSchema(const Json::Value& channels,
                               const std::string& deviceType,
                               int                propertyOrder,
                               const std::string& format,
                               const std::string& title,
                               bool               allowCollapse = false)
{
    Json::Value r;
    r["type"] = "array";
    if (!title.empty()) {
        r["title"] = title;
    }
    r["options"]["disable_array_reorder"] = true;
    if (!allowCollapse) {
        r["options"]["wb"]["disable_title"] = true;
        r["options"]["disable_collapse"] = true;
        r["options"]["wb"]["disable_array_item_panel"] = true;
    } else {
        r["options"]["disable_edit_json"] = true;
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
        auto& items = MakeArray("items", r);
        for (const auto& channel: channels) {
            items.append(MakeTabChannelSchema(channel, deviceType));
        }
        r["minItems"] = items.size();
        r["maxItems"] = items.size();
    } else {
        r["options"]["grid_columns"] = 12;
        r["items"]["$ref"] = "#/definitions/tableChannelSettings";
        auto& defaults = MakeArray("default", r);
        for (const auto& channel: channels) {
            Json::Value& v = Append(defaults);
            v["name"] = channel["name"];
            SetIfExists(v, "enabled", channel, "enabled");
            SetIfExists(v, "poll_interval", channel, "poll_interval");
        }
        r["minItems"] = defaults.size();
        r["maxItems"] = defaults.size();
    }
    if ( format == "list" ) {
        r["options"]["compact"] = true;
    } else {
        if (tabs) {
            r["_format"] = "tabs";
        } else {
            if (allowCollapse) {
                r["options"]["wb"]["disable_panel"] = true;
            } else {
                r["options"]["compact"] = true;
            }
            r["_format"] = "table";
        }
    }
    return r;
}

int AddDeviceParametersUI(Json::Value&       properties,
                          Json::Value&       requiredArray,
                          const Json::Value& deviceTemplate,
                          int                firstParameterOrder)
{
    if (deviceTemplate.isMember("parameters")) {
        const auto& params = deviceTemplate["parameters"];
        for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
            ++firstParameterOrder;
            auto& node = properties[it.name()];
            node = MakeParameterSchema(*it, firstParameterOrder);
            if (IsRequiredSetupRegister(*it)) {
                requiredArray.append(it.name());
            } else {
                node["options"]["show_opt_in"] = true;
            }
        }
    }
    return firstParameterOrder;
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
//              "options": {
//                  "wb": {
//                      "disable_title": true
//                  }
//              },
//              "properties": {
//                  "parameter1": PARAMETER_SCHEMA,
//                  ...
//                  "standard_channels": STANDARD_CHANNELS_SCHEMA
//              },
//              "_format": "grid",
//              "required": [ "parameter1", ... ] // if any
//          }
//      }
//  }
void MakeSubDeviceUISchema(const std::string&      subDeviceType,
                           const TDeviceTemplate&  subdeviceTemplate,
                           const std::string&      deviceType,
                           Json::Value&            definitions)
{
    Json::Value& res = definitions[GetSubdeviceSchemaKey(deviceType, subDeviceType)];
    res["type"] = "object";
    res["title"] = subdeviceTemplate.Title;
    res["options"]["disable_edit_json"] = true;
    res["options"]["disable_properties"] = true;
    res["options"]["disable_collapse"] = true;

    auto set = GetSubdeviceKey(subDeviceType);

    MakeArray("required", res).append(set);

    res["properties"][set]["type"] = "object";
    res["properties"][set]["options"]["wb"]["disable_title"] = true;

    std::string channelsFormat("default");
    std::string channelsTitle;
    if (subdeviceTemplate.Schema.isMember("ui_options")) {
        Get(subdeviceTemplate.Schema["ui_options"], "channels_format", channelsFormat);
    }
    res["properties"][set]["_format"] = "grid";

    int order = 1;
    if (subdeviceTemplate.Schema.isMember("parameters")) {
        Json::Value required(Json::arrayValue);
        order = AddDeviceParametersUI(res["properties"][set]["properties"], required, subdeviceTemplate.Schema, order);
        if (!required.empty()) {
            res["properties"][set]["required"] = required;
        }
    }

    if (subdeviceTemplate.Schema.isMember("channels")) {
        res["properties"][set]["properties"]["standard_channels"] = MakeChannelsSchema(subdeviceTemplate.Schema["channels"],
                                                                                       deviceType,
                                                                                       order,
                                                                                       channelsFormat,
                                                                                       channelsTitle);
    } else {
        // No channels, so it is just a stub device with predefined settings, nothig to show in UI.
        res["properties"][set]["options"]["collapsed"] = true;
    }
}

//  {
//      "type": "object",
//      "title": "Device options",
//      "properties": {
//          "parameter1": PARAMETER_SCHEMA,
//          ...
//      },
//      "options": {
//          "disable_edit_json": true,
//          "disable_properties": true
//      },
//      "propertyOrder": PROPERTY_ORDER
//      "required": [ ... ] // if any
//  }
Json::Value MakeDeviceSettingsUI(const Json::Value& deviceTemplate, int propertyOrder)
{
    Json::Value res;

    res["type"] = "object";
    res["title"] = "Device options";
    res["options"]["disable_edit_json"] = true;
    res["options"]["disable_properties"] = true;
    res["propertyOrder"] = propertyOrder;
    Json::Value req(Json::arrayValue);
    AddDeviceParametersUI(res["properties"], req, deviceTemplate, 0);
    if (!req.empty()) {
        res["required"] = req;
    }
    return res;
}

//  {
//      "type": "object",
//      "title": DEVICE_TITLE,
//      "required": ["DEVICE_HASH"],
//      "options": {
//          "wb": {
//              "disable_title": true
//          }
//      },
//      "properties": {
//          "DEVICE_HASH": {
//              "type": "object",
//              "propertyOrder": 9
//              "options": {
//                  "disable_edit_json": true,
//                  "compact": true,
//                  "wb": {
//                      "disable_panel": true
//                  }
//              },
//              "allOf": [
//                  { "$ref": PROTOCOL_PARAMETERS }
//              ],
//              "properties": {
//                  "standard_channels": STANDARD_CHANNELS_SCHEMA,
//                  "parameters": PARAMETERS_SCHEMA
//              },
//              "defaultProperties": ["slave_id", "standard_channels", "parameters"],
//              "required": ["slave_id"]
//          }
//      }
void  AddDeviceUISchema(const TDeviceTemplate& deviceTemplate,
                        TSerialDeviceFactory&  deviceFactory,
                        Json::Value&           devicesArray,
                        Json::Value&           definitions)
{
    auto set = GetDeviceKey(deviceTemplate.Type);

    auto& res = Append(devicesArray);
    res["type"] = "object";
    res["title"] = deviceTemplate.Title;
    MakeArray("required", res).append(set);

    res["options"]["wb"]["disable_title"] = true;

    Json::Value& pr = res["properties"][set];

    pr["type"] = "object";
    pr["propertyOrder"] = 9;
    pr["options"]["disable_edit_json"] = true;
    pr["options"]["compact"] = true;
    pr["options"]["wb"]["disable_panel"] = true;

    auto& allOf = MakeArray("allOf", pr);
    Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(GetProtocolName(deviceTemplate.Schema));

    std::string channelsFormat("default");
    if (deviceTemplate.Schema.isMember("ui_options")) {
        Get(deviceTemplate.Schema["ui_options"], "channels_format", channelsFormat);
    }

    MakeArray("required", pr).append("slave_id");

    auto& defaultProperties = MakeArray("defaultProperties", pr);
    defaultProperties.append("slave_id");

    if (deviceTemplate.Schema.isMember("channels")) {
        pr["properties"]["standard_channels"] = MakeChannelsSchema(deviceTemplate.Schema["channels"], deviceTemplate.Type, 98, channelsFormat, "Channels", true);
        defaultProperties.append("standard_channels");
    }
    if (deviceTemplate.Schema.isMember("parameters")) {
        pr["properties"]["parameters"] = MakeDeviceSettingsUI(deviceTemplate.Schema, 97);
        defaultProperties.append("parameters");
    }

    TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, deviceTemplate.Schema);
    if (deviceTemplate.Schema.isMember("subdevices")) {
        for (const auto& subDevice: deviceTemplate.Schema["subdevices"]) {
            auto name = subDevice["device_type"].asString();
            MakeSubDeviceUISchema(name, subdeviceTemplates.GetTemplate(name), deviceTemplate.Type, definitions);
        }
    }
}

void AppendDeviceSchemas(Json::Value& devicesArray, Json::Value& definitions, TTemplateMap& templates, TSerialDeviceFactory& deviceFactory)
{
    for (const auto& t: templates.GetTemplatesOrderedByName()) {
        try {
            AddDeviceUISchema(*t, deviceFactory, devicesArray, definitions);
        } catch (const std::exception& e) {
            LOG(Error) << "Can't load template for '" << t->Title << "': " << e.what();
        }
    }
}

Json::Value MakeSchemaForConfed(const Json::Value& configSchema, TTemplateMap& templates, TSerialDeviceFactory& deviceFactory)
{
    Json::Value res(configSchema);
    // Let's add to #/definitions/device/oneOf a list of devices generated from templates
    if (res["definitions"]["device"].isMember("oneOf")) {
        Json::Value newArray(Json::arrayValue);
        AppendDeviceSchemas(newArray, res["definitions"], templates, deviceFactory);
        for (auto& item: res["definitions"]["device"]["oneOf"]) {
            newArray.append(item);
        }
        res["definitions"]["device"]["oneOf"] = newArray;
    }
    res.setComment(std::string("// THIS FILE IS GENERATED BY wb-mqtt-serial SERVICE. DO NOT EDIT IT!"), Json::commentBefore);
    return res;
}
