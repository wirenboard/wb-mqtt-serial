#include "subdevices_config/config_schema_generator.h"
#include "subdevices_config/confed_schema_generator.h"

#include "json_common.h"

using Subdevices::GetSubdeviceSchemaKey;

namespace
{
    bool IsRequiredSetupRegister(const Json::Value& setupRegister)
    {
        return setupRegister.get("required", false).asBool();
    }

    //  {
    //      "type": "number",
    //      "minimum": MIN,
    //      "maximum": MAX,
    //      "enum": [ ... ]
    //  }
    Json::Value MakeParameterSchema(const Json::Value& setupRegister)
    {
        Json::Value r;
        r["type"] = "number";
        SetIfExists(r, "enum", setupRegister, "enum");
        SetIfExists(r, "minimum", setupRegister, "min");
        SetIfExists(r, "maximum", setupRegister, "max");
        return r;
    }

    //  {
    //      "allOf": [ {"$ref": "#/definitions/channelSettings"} ],
    //      "properties": {
    //          "name": {
    //              "type": "string",
    //              "enum": [ CHANNEL_NAME ]
    //          }
    //      },
    //      "required": ["name"]
    //  }
    Json::Value MakeTabSimpleChannelSchema(const Json::Value& channelTemplate)
    {
        Json::Value r;
        MakeArray("allOf", r).append(MakeObject("$ref", "#/definitions/channelSettings"));
        r["properties"]["name"] = MakeSingleValueProperty(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");
        return r;
    }

    //  {
    //      "oneOf": [
    //          {
    //              "allOf": [ { "$ref": "#/definitions/DEVICE_SCHEMA_NAME" } ],
    //              "properties": {
    //                  "device_type": {
    //                      "type": "string",
    //                      "enum": [ DEVICE_TYPE ]
    //                  }
    //              },
    //              "required": [ "device_type" ]
    //          },
    //          ...
    //      ],
    //      "properties": {
    //          "name": {
    //              "type": "string",
    //              "enum": [ CHANNEL_NAME ]
    //          }
    //      },
    //      "required": ["name"]
    //  }
    Json::Value MakeTabOneOfChannelSchema(const Json::Value& channelTemplate)
    {
        Json::Value r;
        r["properties"]["name"] = MakeSingleValueProperty(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");

        auto& items = MakeArray("oneOf", r);
        for (const auto& subDeviceName: channelTemplate["oneOf"]) {
            auto name(subDeviceName.asString());
            Json::Value i;
            i["properties"]["device_type"] = MakeSingleValueProperty(name);
            MakeArray("required", i).append("device_type");
            MakeArray("allOf", i).append(MakeObject("$ref", "#/definitions/" + GetSubdeviceSchemaKey(name)));
            items.append(i);
        }

        return r;
    }

    //  {
    //      "allOf": [ { "$ref": "#/definitions/DEVICE_SCHEMA_NAME" } ],
    //      "properties": {
    //          "name": {
    //              "type": "string",
    //              "enum": [ CHANNEL_NAME ]
    //          }
    //      },
    //      "required": ["name"]
    //  }
    Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channelTemplate)
    {
        Json::Value r;
        MakeArray("allOf", r)
            .append(MakeObject("$ref",
                               "#/definitions/" + GetSubdeviceSchemaKey(channelTemplate["device_type"].asString())));
        r["properties"]["name"] = MakeSingleValueProperty(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");
        return r;
    }

    Json::Value MakeTabChannelSchema(const Json::Value& channel)
    {
        if (channel.isMember("oneOf")) {
            return MakeTabOneOfChannelSchema(channel);
        }
        if (channel.isMember("device_type")) {
            return MakeTabSingleDeviceChannelSchema(channel);
        }
        return MakeTabSimpleChannelSchema(channel);
    }

    //  {
    //      "allOf": [
    //          {
    //              "not": {
    //                  "properties": {
    //                      "name": {
    //                          "type": "string",
    //                          "enum": [ CHANNEL_NAMES ]
    //                      }
    //                  }
    //              }
    //          },
    //          {
    //              "oneOf": [
    //                  { "$ref": CUSTOM_CHANNEL_SCHEMA },
    //                  We have old configs with channels missing in new template versions
    //                  Accept those configs and process errors by wb-mqtt-serial's code not by schema
    //                  This is not a good solution. Should be deleted after template versions implementation
    //                  {
    //                      "properties": {
    //                          "name": { "type": "string" },
    //                          "address": { "not": {} },
    //                          "consists_of": { "not": {} }
    //                      }
    //                  }
    //              ]
    //          }
    //      ]
    //  }
    Json::Value MakeCustomChannelsSchema(const std::vector<std::string>& names,
                                         const std::string& customChannelsSchemaRef)
    {
        Json::Value n;
        n["not"]["properties"]["name"]["type"] = "string";
        auto& en = MakeArray("enum", n["not"]["properties"]["name"]);
        for (const auto& name: names) {
            en.append(name);
        }

        Json::Value channelTypes;
        auto& oneOf = MakeArray("oneOf", channelTypes);
        oneOf.append(MakeObject("$ref", customChannelsSchemaRef));
        Json::Value oldChannels;
        oldChannels["properties"]["name"]["type"] = "string";
        oldChannels["properties"]["address"]["not"] = Json::Value(Json::objectValue);
        oldChannels["properties"]["consists_of"]["not"] = Json::Value(Json::objectValue);
        oneOf.append(std::move(oldChannels));

        Json::Value r;
        auto& allOf = MakeArray("allOf", r);
        allOf.append(std::move(n));
        allOf.append(std::move(channelTypes));
        return r;
    }

    //  {
    //      "type": "array",
    //      "items": {
    //          "oneOf": [
    //              CHANNELS_SCHEMAS,
    //              CUSTOM_CHANNELS_SCHEMA
    //          ]
    //      }
    //  }
    Json::Value MakeDeviceChannelsSchema(const Json::Value& channels, const std::string& customChannelsSchemaRef)
    {
        Json::Value r;
        std::vector<std::string> names;

        r["type"] = "array";
        auto& items = MakeArray("oneOf", r["items"]);
        for (const auto& channel: channels) {
            auto channelSchema = MakeTabChannelSchema(channel);
            items.append(channelSchema);
            names.push_back(channel["name"].asString());
        }
        if (!names.empty() && !customChannelsSchemaRef.empty()) {
            items.append(MakeCustomChannelsSchema(names, customChannelsSchemaRef));
        }
        return r;
    }

    void MakeDeviceParametersSchema(Json::Value& properties,
                                    Json::Value& requiredArray,
                                    const Json::Value& deviceTemplate)
    {
        if (deviceTemplate.isMember("parameters")) {
            const auto& params = deviceTemplate["parameters"];
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                auto name = it.name();
                if (properties.isMember(name)) {
                    throw std::runtime_error("Validation failed.\nError 1\n  context: <root>\n  desc: "
                                             "duplicate definition of parameter \"" +
                                             name + "\" in device template");
                }
                properties[name] = MakeParameterSchema(*it);
                if (IsRequiredSetupRegister(*it)) {
                    requiredArray.append(name);
                }
            }
        }
    }

    //  {
    //      "type": "object",
    //      "properties": {
    //          "device_type": {
    //              "type": "string",
    //              "enum": [ DEVICE_TYPE ]
    //          },
    //          "parameter1": PARAMETER_SCHEMA,
    //          ...
    //          "channels": CHANNELS_SCHEMA
    //      },
    //      "required": [ "parameter1", ... ]
    //  }
    Json::Value MakeSubDeviceSchema(const std::string& subDeviceType, const TSubDeviceTemplate& subdeviceTemplate)
    {
        Json::Value res;
        res["type"] = "object";
        res["properties"]["device_type"] = MakeSingleValueProperty(subDeviceType);

        if (subdeviceTemplate.Schema.isMember("parameters")) {
            Json::Value req(Json::arrayValue);
            MakeDeviceParametersSchema(res["properties"], req, subdeviceTemplate.Schema);
            if (!req.empty()) {
                res["required"] = req;
            }
        }

        if (subdeviceTemplate.Schema.isMember("channels")) {
            res["properties"]["channels"]["type"] = "array";
            auto& items = MakeArray("oneOf", res["properties"]["channels"]["items"]);
            for (const auto& channel: subdeviceTemplate.Schema["channels"]) {
                items.append(MakeTabChannelSchema(channel));
            }
        }

        return res;
    }
}

//  {
//      "type": "object",
//      "allOf": [
//          { "$ref": COMMON_DEVICE_SCHEMA }
//      ],
//      "properties": {
//          "device_type": {
//              "type": "string",
//              "enum": [ DEVICE_TYPE ]
//          },
//          "parameter1": PARAMETER_SCHEMA,
//          ...
//          "channels": CHANNELS_SCHEMA
//      },
//      "required": ["device_type", "slave_id", "parameter1", ...]
//  }
Json::Value Subdevices::MakeSchemaForDeviceConfigValidation(const Json::Value& commonDeviceSchema,
                                                            TDeviceTemplate& deviceTemplate,
                                                            TSerialDeviceFactory& deviceFactory)
{
    auto schema(commonDeviceSchema);
    auto protocolName = GetProtocolName(deviceTemplate.GetTemplate());

    auto& req = MakeArray("required", schema);
    req.append("device_type");
    if (!deviceFactory.GetProtocol(protocolName)->SupportsBroadcast()) {
        req.append("slave_id");
    }

    schema["properties"] = Json::Value(Json::objectValue);
    schema["properties"]["device_type"] = MakeSingleValueProperty(deviceTemplate.Type);

    if (deviceTemplate.GetTemplate().isMember("parameters")) {
        MakeDeviceParametersSchema(schema["properties"], req, deviceTemplate.GetTemplate());
    }

    if (deviceTemplate.GetTemplate().isMember("channels")) {
        auto customChannelsSchemaRef = deviceFactory.GetCustomChannelSchemaRef(protocolName);
        schema["properties"]["channels"] =
            MakeDeviceChannelsSchema(deviceTemplate.GetTemplate()["channels"], customChannelsSchemaRef);
    }

    auto deviceSchemaRef = deviceFactory.GetCommonDeviceSchemaRef(protocolName);
    const auto NO_CHANNELS_SUFFIX = "_no_channels";
    if (!WBMQTT::StringHasSuffix(deviceSchemaRef, NO_CHANNELS_SUFFIX)) {
        deviceSchemaRef += NO_CHANNELS_SUFFIX;
    }
    MakeArray("allOf", schema).append(MakeObject("$ref", deviceSchemaRef));

    TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, deviceTemplate.GetTemplate());
    for (const auto& subDevice: deviceTemplate.GetTemplate()["subdevices"]) {
        auto name = subDevice["device_type"].asString();
        schema["definitions"][GetSubdeviceSchemaKey(name)] =
            MakeSubDeviceSchema(name, subdeviceTemplates.GetTemplate(name));
    }
    return schema;
}
