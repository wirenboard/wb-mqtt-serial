#include "config_schema_generator.h"

#include "config_merge_template.h"
#include "expression_evaluator.h"
#include "json_common.h"

#include "subdevices_config/config_schema_generator.h"

using namespace WBMQTT::JSON;
using Expressions::TExpressionsCache;

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
    Json::Value MakeTabChannelSchema(const Json::Value& channelTemplate)
    {
        Json::Value r;
        MakeArray("allOf", r).append(MakeObject("$ref", "#/definitions/channelSettings"));
        r["properties"]["name"] = MakeSingleValueProperty(channelTemplate["name"].asString());
        MakeArray("required", r).append("name");
        return r;
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

    void MakeDeviceParametersSchema(const Json::Value& config,
                                    Json::Value& properties,
                                    Json::Value& requiredArray,
                                    const Json::Value& deviceTemplate,
                                    TExpressionsCache& exprCache)
    {
        TJsonParams exprParams(config);
        if (deviceTemplate.isMember("parameters")) {
            const auto& params = deviceTemplate["parameters"];
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                auto name = params.isArray() ? (*it)["id"].asString() : it.name();
                if (CheckCondition(*it, exprParams, &exprCache)) {
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
    Json::Value MakeSchemaForDeviceConfigValidation(const Json::Value& commonDeviceSchema,
                                                    const Json::Value& deviceConfig,
                                                    TDeviceTemplate& deviceTemplate,
                                                    TSerialDeviceFactory& deviceFactory,
                                                    TExpressionsCache& exprCache)
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
            MakeDeviceParametersSchema(deviceConfig,
                                       schema["properties"],
                                       req,
                                       deviceTemplate.GetTemplate(),
                                       exprCache);
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
        return schema;
    }

    std::string ReplaceSubstrings(std::string str, const std::string& pattern, const std::string& repl)
    {
        size_t index = 0;
        while (pattern.length()) {
            index = str.find(pattern, index);
            if (index == std::string::npos)
                break;
            str.replace(index, pattern.length(), repl);
            index += repl.length();
        }
        return str;
    }

    class TDeviceTypeValidator
    {
        const Json::Value& CommonDeviceSchema;
        TTemplateMap& Templates;
        TSerialDeviceFactory& DeviceFactory;
        TExpressionsCache ExprCache;

    public:
        TDeviceTypeValidator(const Json::Value& commonDeviceSchema,
                             TTemplateMap& templates,
                             TSerialDeviceFactory& deviceFactory)
            : CommonDeviceSchema(commonDeviceSchema),
              Templates(templates),
              DeviceFactory(deviceFactory)
        {}

        void Validate(const Json::Value& deviceConfig)
        {
            auto deviceTemplate = Templates.GetTemplate(deviceConfig["device_type"].asString());
            auto schema = deviceTemplate->WithSubdevices()
                              ? Subdevices::MakeSchemaForDeviceConfigValidation(CommonDeviceSchema,
                                                                                *deviceTemplate,
                                                                                DeviceFactory)
                              : MakeSchemaForDeviceConfigValidation(CommonDeviceSchema,
                                                                    deviceConfig,
                                                                    *deviceTemplate,
                                                                    DeviceFactory,
                                                                    ExprCache);

            ::Validate(deviceConfig, schema);
        }
    };

    class TProtocolValidator
    {
        // Use the same schema for validation and confed
        TProtocolConfedSchemasMap& Schemas;

        //! Device type to validator for custom devices declared with protocol property
        std::unordered_map<std::string, std::shared_ptr<TValidator>> validators;

    public:
        TProtocolValidator(TProtocolConfedSchemasMap& protocolSchemas): Schemas(protocolSchemas)
        {}

        void Validate(const Json::Value& deviceConfig)
        {
            auto protocol = deviceConfig.get("protocol", "modbus").asString();
            auto protocolIt = validators.find(protocol);
            if (protocolIt != validators.end()) {
                protocolIt->second->Validate(deviceConfig);
                return;
            }
            auto validator = std::make_shared<TValidator>(Schemas.GetSchema(protocol));
            validators.insert({protocol, validator});
            validator->Validate(deviceConfig);
        }
    };

}

void ValidateConfig(const Json::Value& config,
                    TSerialDeviceFactory& deviceFactory,
                    const Json::Value& commonDeviceSchema,
                    const Json::Value& portsSchema,
                    TTemplateMap& templates,
                    TProtocolConfedSchemasMap& protocolSchemas)
{
    Validate(config, portsSchema);

    TProtocolValidator protocolValidator(protocolSchemas);
    TDeviceTypeValidator deviceTypeValidator(commonDeviceSchema, templates, deviceFactory);
    size_t portIndex = 0;
    for (const auto& port: config["ports"]) {
        size_t deviceIndex = 0;
        for (const auto& device: port["devices"]) {
            try {
                if (device.isMember("device_type")) {
                    deviceTypeValidator.Validate(device);
                } else {
                    protocolValidator.Validate(device);
                }
            } catch (const std::runtime_error& e) {
                std::stringstream newContext;
                newContext << "<root>[ports][" << portIndex << "][devices][" << deviceIndex << "]";
                throw std::runtime_error(ReplaceSubstrings(e.what(), "<root>", newContext.str()));
            }
            ++deviceIndex;
        }
        ++portIndex;
    }
}
