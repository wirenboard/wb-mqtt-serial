#include "config_schema_generator.h"
#include "confed_schema_generator.h"
#include "json_common.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace WBMQTT::JSON;

namespace
{
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
        SetIfExists(r, "enum",    setupRegister, "enum");
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
        r["properties"]["name"] = MakeSingleValuePropery(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");
        return r;
    }

    //  {
    //      "oneOf": [
    //          { "$ref": "#/definitions/DEVICE_SCHEMA_NAME" },
    //          ...
    //      ],
    //      "properties": {
    //          "name": {
    //              "type": "string",
    //              "enum": [ CHANNEL_NAME ]
    //          }
    //      },
    //      "required": ["name", "device_type"]
    //  }
    Json::Value MakeTabOneOfChannelSchema(const Json::Value& channelTemplate, const std::string& deviceType)
    {
        Json::Value r;
        r["properties"]["name"] = MakeSingleValuePropery(channelTemplate["name"].asString());
        auto& req = MakeArray("required", r);
        req.append("name");
        req.append("device_type");

        auto& items = MakeArray("oneOf", r);
        for (const auto& subDeviceName: channelTemplate["oneOf"]) {
            items.append(MakeObject("$ref", "#/definitions/" + GetSubdeviceSchemaKey(deviceType, subDeviceName.asString())));
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
    Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channelTemplate, const std::string& deviceType)
    {
        Json::Value r;
        MakeArray("allOf", r).append(MakeObject("$ref", "#/definitions/" + GetSubdeviceSchemaKey(deviceType, channelTemplate["device_type"].asString())));
        r["properties"]["name"] = MakeSingleValuePropery(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");
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

    //  {
    //      "allOf": [
    //          { "$ref": CUSTOM_CHANNEL_SCHEMA },
    //          {
    //              "not": {
    //                  "properties": {
    //                      "name": {
    //                          "type": "string",
    //                          "enum": [ CHANNEL_NAMES ]
    //                      }
    //                  }
    //              }
    //          }
    //      ]
    //  }
    Json::Value MakeCustomChannelsSchema(const std::vector<std::string>& names, const std::string& customChannelsSchemaRef)
    {
        Json::Value n;
        n["not"]["properties"]["name"]["type"] = "string";
        auto& en = MakeArray("enum", n["not"]["properties"]["name"]);
        for (const auto& name: names) {
            en.append(name);
        }
        Json::Value r;
        auto& allOf = MakeArray("allOf", r);
        allOf.append(MakeObject("$ref", customChannelsSchemaRef));
        allOf.append(n);
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
    Json::Value MakeDeviceChannelsSchema(const Json::Value& channels, const std::string& deviceType, const std::string& customChannelsSchemaRef)
    {
        Json::Value r;
        r["type"] = "array";
        auto& items = MakeArray("oneOf", r["items"]);
        std::vector<std::string> names;
        for (const auto& channel: channels) {
            auto channelSchema = MakeTabChannelSchema(channel, deviceType);
            items.append(channelSchema);
            names.push_back(channel["name"].asString());
        }
        if (!names.empty() && !customChannelsSchemaRef.empty()) {
            items.append(MakeCustomChannelsSchema(names, customChannelsSchemaRef));
        }
        return r;
    }

    void MakeDeviceParametersSchema(Json::Value& properties, Json::Value& requiredArray, const Json::Value& deviceTemplate)
    {
        if (deviceTemplate.isMember("parameters")) {
            const auto& params = deviceTemplate["parameters"];
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                properties[it.name()] = MakeParameterSchema(*it);
                if (IsRequiredSetupRegister(*it)) {
                    requiredArray.append(it.name());
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
    Json::Value MakeSubDeviceSchema(const std::string&      subDeviceType,
                                    const TDeviceTemplate&  subdeviceTemplate,
                                    const std::string&      deviceType)
    {
        Json::Value res;
        res["type"] = "object";
        res["properties"]["device_type"] = MakeSingleValuePropery(subDeviceType);

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
                items.append(MakeTabChannelSchema(channel, deviceType));
            }
        }

        return res;
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
    //      "required": ["device_type", "slave_id"]
    //  }
    void AddDeviceSchema(const TDeviceTemplate& deviceTemplate,
                         TSerialDeviceFactory& deviceFactory,
                         Json::Value& devicesArray,
                         Json::Value& definitions)
    {
        Json::Value res;
        res["type"] = "object";

        auto& req = MakeArray("required", res);
        req.append("device_type");
        req.append("slave_id");

        res["properties"]["device_type"] = MakeSingleValuePropery(deviceTemplate.Type);

        if (deviceTemplate.Schema.isMember("parameters")) {
            MakeDeviceParametersSchema(res["properties"], req, deviceTemplate.Schema);
        }

        auto protocolName = GetProtocolName(deviceTemplate.Schema);

        if (deviceTemplate.Schema.isMember("channels")) {
            auto customChannelsSchemaRef = deviceFactory.GetCustomChannelSchemaRef(protocolName);
            res["properties"]["channels"] = MakeDeviceChannelsSchema(deviceTemplate.Schema["channels"], deviceTemplate.Type, customChannelsSchemaRef);
        }

        auto deviceSchemaRef = deviceFactory.GetCommonDeviceSchemaRef(protocolName);
        const auto NO_CHANNELS_SUFFIX = "_no_channels";
        if (!WBMQTT::StringHasSuffix(deviceSchemaRef, NO_CHANNELS_SUFFIX)) {
            deviceSchemaRef += NO_CHANNELS_SUFFIX;
        }
        MakeArray("allOf", res).append(MakeObject("$ref", deviceSchemaRef));

        TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, deviceTemplate.Schema);
        if (deviceTemplate.Schema.isMember("subdevices")) {
            for (const auto& subDevice: deviceTemplate.Schema["subdevices"]) {
                auto name = subDevice["device_type"].asString();
                definitions[GetSubdeviceSchemaKey(deviceTemplate.Type, name)] = MakeSubDeviceSchema(name, subdeviceTemplates.GetTemplate(name), deviceTemplate.Type);
            }
        }
        devicesArray.append(res);
    }

    void AppendDeviceSchemas(const std::unordered_set<std::string>& deviceTypes,
                             Json::Value& devicesArray,
                             Json::Value& definitions,
                             TTemplateMap& templates,
                             TSerialDeviceFactory& deviceFactory)
    {
        for (const auto& deviceType: deviceTypes) {
            AddDeviceSchema(templates.GetTemplate(deviceType), deviceFactory, devicesArray, definitions);
        }
    }
}

Json::Value MakeSchemaForConfigValidation(const Json::Value&              baseConfigSchema,
                                          const TConfigValidationOptions& options,
                                          TTemplateMap&                   templates,
                                          TSerialDeviceFactory&           deviceFactory)
{
    Json::Value res(baseConfigSchema);
    // Let's add to #/definitions/device/oneOf a list of devices generated from templates
    if (res["definitions"]["device"].isMember("oneOf")) {
        Json::Value newArray(Json::arrayValue);
        AppendDeviceSchemas(options.DeviceTypes, newArray, res["definitions"], templates, deviceFactory);
        // Let's remove protocols not used in config
        for (const auto& item: res["definitions"]["device"]["oneOf"]) {
            if (item["properties"].isMember("protocol")) {
                if (options.Protocols.count(item["properties"]["protocol"]["enum"][0].asString())) {
                    newArray.append(item);
                }
            }
        }
        res["definitions"]["device"]["oneOf"] = newArray;
    }
    return res;
}

TConfigValidationOptions GetValidationDeviceTypes(const Json::Value& config)
{
    TConfigValidationOptions res;
    if (config.isMember("ports")) {
        for(const auto& port: config["ports"]) {
            if (port.isMember("devices")) {
                for(const auto& device: port["devices"]) {
                    if (device.isMember("device_type")) {
                        res.DeviceTypes.insert(device["device_type"].asString());
                    } else if (device.isMember("protocol")) {
                        res.Protocols.insert(device["protocol"].asString());
                    } else {
                        res.Protocols.insert("modbus");
                    }
                }
            }
        }
    }
    if (res.Protocols.empty() && res.DeviceTypes.empty()) {
        res.Protocols.insert("modbus");
    }
    return res;
}
