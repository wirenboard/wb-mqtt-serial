#include "confed_schema_generator.h"
#include "json_common.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace WBMQTT::JSON;

namespace
{
    //! json-editor has maximum 12 columns in grid layout, derived from Bootstrap's 12 columns layout
    const auto MAX_GRID_COLUMNS = 12;

    const auto DEVICE_PARAMETERS_PROPERTY_ORDER = 97;

    std::string GetHashedParam(const std::string& deviceType, const std::string& prefix)
    {
        return prefix + "_" + std::to_string(std::hash<std::string>()(deviceType));
    }

    std::string GetTranslationHash(const std::string& deviceType, const std::string& msg)
    {
        return "t" + std::to_string(std::hash<std::string>()(deviceType + msg));
    }

    struct TContext
    {
        const std::string& DeviceType;
        Json::Value& Translations;

        /**
         * @brief Calculate hash from msg and DeviceType.
         *        Add msg in "en" translations list with calculated hash as key
         *
         * @param msg message to add to translations list
         * @return std::string key of msg in translations list
         */
        std::string AddHashedTranslation(const std::string& msg)
        {
            auto hash = GetTranslationHash(DeviceType, msg);
            Translations["en"][hash] = msg;
            return hash;
        }
    };

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
    //      "default": VALUE,
    //      "options": { "hidden": true }
    //  }
    Json::Value MakeHiddenProperty(const std::string& value)
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
        r["properties"]["name"] = MakeHiddenProperty(name);
        MakeArray("required", r).append("name");
        return r;
    }

    //  {
    //      "properties": {
    //          "protocol": {
    //              "type": "string",
    //              "options": {
    //                  "hidden": true
    //              }
    //          }
    //      }
    //  }
    Json::Value MakeProtocolProperty()
    {
        Json::Value r;
        r["properties"]["protocol"]["type"] = "string";
        r["properties"]["protocol"]["options"]["hidden"] = true;
        return r;
    }

    //  {
    //      "type": "number", // or "type": "integer"
    //      "title": TITLE_HASH,
    //      "default": DEFAULT,
    //      "minimum": MIN,
    //      "maximum": MAX,
    //      "enum": [ ... ],
    //      "description": DESCRIPTION_HASH,
    //      "propertyOrder": INDEX
    //      "options": {
    //          "enumTitles" : [ ... ],
    //      },
    //  }
    Json::Value MakeParameterSchema(const Json::Value& setupRegister, int index, TContext& context)
    {
        Json::Value r;
        r["type"] = setupRegister.isMember("scale") ? "number" : "integer";
        r["title"] = context.AddHashedTranslation(setupRegister["title"].asString());
        r["default"] = GetDefaultSetupRegisterValue(setupRegister);
        SetIfExists(r, "enum", setupRegister, "enum");
        SetIfExists(r, "minimum", setupRegister, "min");
        SetIfExists(r, "maximum", setupRegister, "max");
        if (setupRegister.isMember("description")) {
            r["description"] = context.AddHashedTranslation(setupRegister["description"].asString());
        }
        r["propertyOrder"] = index;
        if (setupRegister.isMember("enum_titles")) {
            auto& titles = MakeArray("enum_titles", r["options"]);
            for (const auto& title: setupRegister["enum_titles"]) {
                titles.append(context.AddHashedTranslation(title.asString()));
            }
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
    //      "headerTemplate": CHANNEL_NAME_HASH,
    //      "options": {
    //          "wb": { "disable_title": true }
    //      },
    //      "default": {
    //        "name": CHANNEL_NAME,
    //        "enabled": CHANNEL_ENABLED,
    //        "poll_interval": CHANNEL_POLL_INTERVAL
    //      }
    //  }
    Json::Value MakeTabSimpleChannelSchema(const Json::Value& channelTemplate, TContext& context)
    {
        Json::Value r;
        auto& allOf = MakeArray("allOf", r);
        allOf.append(MakeObject("$ref", "#/definitions/channelSettings"));
        allOf.append(MakeHiddenNameObject(channelTemplate["name"].asString()));
        r["headerTemplate"] = context.AddHashedTranslation(channelTemplate["name"].asString());
        r["default"]["name"] = channelTemplate["name"].asString();
        r["options"]["wb"]["disable_title"] = true;
        r["default"]["enabled"] = true;
        SetIfExists(r["default"], "enabled", channelTemplate, "enabled");
        SetIfExists(r["default"], "poll_interval", channelTemplate, "poll_interval");
        return r;
    }

    //  {
    //      "headerTemplate": CHANNEL_NAME_HASH,
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
    Json::Value MakeTabOneOfChannelSchema(const Json::Value& channel, TContext& context)
    {
        Json::Value r;
        r["headerTemplate"] = context.AddHashedTranslation(channel["name"].asString());
        r["options"]["keep_oneof_values"] = false;
        r["options"]["wb"]["disable_title"] = true;
        auto& allOf = MakeArray("allOf", r);

        auto& oneOf = MakeArray("oneOf", Append(allOf));
        for (const auto& subDeviceName: channel["oneOf"]) {
            Append(oneOf)["$ref"] =
                "#/definitions/" + GetSubdeviceSchemaKey(context.DeviceType, subDeviceName.asString());
        }
        allOf.append(MakeHiddenNameObject(channel["name"].asString()));
        return r;
    }

    //  {
    //      "headerTemplate": CHANNEL_NAME_HASH,
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
    //              "disable_title": true
    //          }
    //      }
    //  }
    Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channel, TContext& context)
    {
        Json::Value r;
        r["headerTemplate"] = context.AddHashedTranslation(channel["name"].asString());
        r["options"]["wb"]["disable_title"] = true;
        auto& allOf = MakeArray("allOf", r);
        Append(allOf)["$ref"] =
            "#/definitions/" + GetSubdeviceSchemaKey(context.DeviceType, channel["device_type"].asString());
        allOf.append(MakeHiddenNameObject(channel["name"].asString()));
        return r;
    }

    Json::Value MakeTabChannelSchema(const Json::Value& channel, TContext& context)
    {
        if (channel.isMember("oneOf")) {
            return MakeTabOneOfChannelSchema(channel, context);
        }
        if (channel.isMember("device_type")) {
            return MakeTabSingleDeviceChannelSchema(channel, context);
        }
        return MakeTabSimpleChannelSchema(channel, context);
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
    Json::Value MakeChannelsSchema(const Json::Value& deviceTemplate,
                                   int propertyOrder,
                                   const std::string& title,
                                   TContext& context,
                                   bool allowCollapse = false)
    {
        const auto& channels = deviceTemplate["channels"];
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

        bool tabs = deviceTemplate.isMember("groups");
        std::string format("default");
        if (deviceTemplate.isMember("ui_options")) {
            Get(deviceTemplate["ui_options"], "channels_format", format);
        }
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
                items.append(MakeTabChannelSchema(channel, context));
            }
            r["minItems"] = items.size();
            r["maxItems"] = items.size();
        } else {
            r["options"]["grid_columns"] = MAX_GRID_COLUMNS;
            r["items"]["$ref"] = "#/definitions/tableChannelSettings";
            auto& defaults = MakeArray("default", r);
            for (const auto& channel: channels) {
                Json::Value& v = Append(defaults);
                v["name"] = channel["name"];
                v["title"] = context.AddHashedTranslation(channel["name"].asString());
                v["enabled"] = true;
                SetIfExists(v, "enabled", channel, "enabled");
                SetIfExists(v, "poll_interval", channel, "poll_interval");
            }
            r["minItems"] = defaults.size();
            r["maxItems"] = defaults.size();
        }
        if (format == "list") {
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

    int AddDeviceParametersUI(Json::Value& properties,
                              Json::Value& requiredArray,
                              const Json::Value& deviceTemplate,
                              int firstParameterOrder,
                              TContext& context)
    {
        int maxOrder = 0;
        if (deviceTemplate.isMember("parameters")) {
            const auto& params = deviceTemplate["parameters"];
            int n = 0;
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                auto& node = properties[it.name()];
                int order = it->get("order", n).asInt();
                maxOrder = std::max(order, maxOrder);
                node = MakeParameterSchema(*it, firstParameterOrder + order, context);
                if (IsRequiredSetupRegister(*it)) {
                    requiredArray.append(it.name());
                } else {
                    node["options"]["show_opt_in"] = true;
                }
                ++n;
            }
        }
        return firstParameterOrder + maxOrder + 1;
    }

    //  {
    //      "type": "object",
    //      "title": DEVICE_TITLE_HASH,
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
    void MakeSubDeviceUISchema(const std::string& subDeviceType,
                               const TDeviceTemplate& subdeviceTemplate,
                               Json::Value& definitions,
                               TContext& context)
    {
        Json::Value& res = definitions[GetSubdeviceSchemaKey(context.DeviceType, subDeviceType)];
        res["type"] = "object";
        res["title"] = context.AddHashedTranslation(subdeviceTemplate.Title);
        res["options"]["disable_edit_json"] = true;
        res["options"]["disable_properties"] = true;
        res["options"]["disable_collapse"] = true;

        auto set = GetSubdeviceKey(subDeviceType);

        MakeArray("required", res).append(set);

        res["properties"][set]["type"] = "object";
        res["properties"][set]["options"]["wb"]["disable_title"] = true;
        res["properties"][set]["_format"] = "grid";

        int order = 1;
        if (subdeviceTemplate.Schema.isMember("parameters")) {
            Json::Value required(Json::arrayValue);
            order = AddDeviceParametersUI(res["properties"][set]["properties"],
                                          required,
                                          subdeviceTemplate.Schema,
                                          order,
                                          context);
            if (!required.empty()) {
                res["properties"][set]["required"] = required;
            }
        }

        if (subdeviceTemplate.Schema.isMember("channels")) {
            res["properties"][set]["properties"]["standard_channels"] =
                MakeChannelsSchema(subdeviceTemplate.Schema, order, "", context);
        } else {
            if (!subdeviceTemplate.Schema.isMember("parameters")) {
                // No channels and parameters,
                // so it is just a stub device with predefined settings, nothig to show in UI.
                res["properties"][set]["options"]["collapsed"] = true;
            }
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
    Json::Value MakeDeviceSettingsUI(const Json::Value& deviceTemplate, int propertyOrder, TContext& context)
    {
        Json::Value res;

        res["type"] = "object";
        res["title"] = "Device options";
        res["options"]["disable_edit_json"] = true;
        res["options"]["disable_properties"] = true;
        res["propertyOrder"] = propertyOrder;
        Json::Value req(Json::arrayValue);
        AddDeviceParametersUI(res["properties"], req, deviceTemplate, 0, context);
        if (!req.empty()) {
            res["required"] = req;
        }
        return res;
    }

    //  {
    //      "type": "object",
    //      "title": DEVICE_TITLE_HASH,
    //      "required": ["DEVICE_HASH"],
    //      "options": {
    //          "wb": {
    //              "disable_title": true
    //          }
    //      },
    //      "properties": {
    //          "DEVICE_HASH": {
    //              "type": "object",
    //              "options": {
    //                  "disable_edit_json": true,
    //                  "compact": true,
    //                  "wb": {
    //                      "disable_panel": true
    //                  }
    //              },
    //              "allOf": [
    //                  { "$ref": PROTOCOL_PARAMETERS },
    //                  { HIDDEN_PROTOCOL }
    //              ],
    //              "properties": {
    //                  "standard_channels": STANDARD_CHANNELS_SCHEMA,
    //                  "parameters": PARAMETERS_SCHEMA
    //              },
    //              "defaultProperties": ["slave_id", "standard_channels", "parameters"],
    //              "required": ["slave_id"]
    //          }
    //      }
    void AddDeviceUISchema(const TDeviceTemplate& deviceTemplate,
                           TSerialDeviceFactory& deviceFactory,
                           Json::Value& devicesArray,
                           Json::Value& definitions,
                           Json::Value& translations)
    {
        Json::Value schema(deviceTemplate.Schema);
        if (!schema.isMember("subdevices")) {
            schema["subdevices"] = Json::Value(Json::arrayValue);
        }
        TransformGroupsToSubdevices(schema, schema["subdevices"]);

        auto set = GetDeviceKey(deviceTemplate.Type);
        TContext context{deviceTemplate.Type, translations};
        auto& res = Append(devicesArray);
        res["type"] = "object";
        res["title"] = context.AddHashedTranslation(deviceTemplate.Title);
        MakeArray("required", res).append(set);

        res["options"]["wb"]["disable_title"] = true;

        Json::Value& pr = res["properties"][set];

        pr["type"] = "object";
        pr["options"]["disable_edit_json"] = true;
        pr["options"]["compact"] = true;
        pr["options"]["wb"]["disable_panel"] = true;

        auto& allOf = MakeArray("allOf", pr);
        auto protocol = GetProtocolName(schema);
        Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(protocol);
        allOf.append(MakeProtocolProperty());

        if (!deviceFactory.GetProtocol(protocol)->SupportsBroadcast()) {
            MakeArray("required", pr).append("slave_id");
        }

        auto& defaultProperties = MakeArray("defaultProperties", pr);
        defaultProperties.append("slave_id");

        if (schema.isMember("channels")) {
            pr["properties"]["standard_channels"] =
                MakeChannelsSchema(schema, DEVICE_PARAMETERS_PROPERTY_ORDER + 1, "Channels", context, true);
            defaultProperties.append("standard_channels");
        }
        if (schema.isMember("parameters") && !schema["parameters"].empty()) {
            pr["properties"]["parameters"] = MakeDeviceSettingsUI(schema, DEVICE_PARAMETERS_PROPERTY_ORDER, context);
            defaultProperties.append("parameters");
        }

        TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, schema);
        if (schema.isMember("subdevices")) {
            for (const auto& subDevice: schema["subdevices"]) {
                auto name = subDevice["device_type"].asString();
                MakeSubDeviceUISchema(name, subdeviceTemplates.GetTemplate(name), definitions, context);
            }
        }
    }

    void AddTranslations(const std::string& deviceType, Json::Value& translations, const Json::Value& deviceSchema)
    {
        const auto& tr = deviceSchema["translations"];
        for (auto it = tr.begin(); it != tr.end(); ++it) {
            for (auto msgIt = it->begin(); msgIt != it->end(); ++msgIt) {
                translations[it.name()][GetTranslationHash(deviceType, msgIt.name())] = *msgIt;
            }
        }
    }

    void AppendDeviceSchemas(Json::Value& devicesArray,
                             Json::Value& definitions,
                             Json::Value& translations,
                             TTemplateMap& templates,
                             TSerialDeviceFactory& deviceFactory)
    {
        for (const auto& t: templates.GetTemplatesOrderedByName()) {
            try {
                AddDeviceUISchema(*t, deviceFactory, devicesArray, definitions, translations);
                AddTranslations(t->Type, translations, t->Schema);
            } catch (const std::exception& e) {
                LOG(Error) << "Can't load template for '" << t->Title << "': " << e.what();
            }
        }
    }

    std::vector<const Json::Value*> PartitionChannelsByGroups(
        const Json::Value& schema,
        std::unordered_map<std::string, Json::Value>& subdevicesForGroups)
    {
        std::vector<const Json::Value*> originalChannels;
        for (const auto& channel: schema["channels"]) {
            try {
                subdevicesForGroups.at(channel["group"].asString())["device"]["channels"].append(channel);
            } catch (...) {
                originalChannels.emplace_back(&channel);
            }
        }
        return originalChannels;
    }

    std::vector<std::string> PartitionParametersByGroups(
        const Json::Value& schema,
        std::unordered_map<std::string, Json::Value>& subdevicesForGroups)
    {
        std::vector<std::string> movedParameters;
        for (auto it = schema["parameters"].begin(); it != schema["parameters"].end(); ++it) {
            try {
                subdevicesForGroups.at((*it)["group"].asString())["device"]["parameters"][it.name()] = *it;
                movedParameters.emplace_back(it.name());
            } catch (...) {
            }
        }
        return movedParameters;
    }
    struct TGroup
    {
        uint32_t Order;
        std::string Name;

        TGroup(const Json::Value& group): Order(group.get("order", 1).asUInt()), Name(group["title"].asString())
        {}
    };

    /**
     * @brief Merge channels according to they order.
     *
     * @param channelsNotInGroups - channels not in groups
     * @param groups - vector of groups sorted by Order
     * @return Json::Value - merged channels array
     */
    Json::Value MergeChannels(const std::vector<const Json::Value*>& channelsNotInGroups,
                              const std::vector<TGroup>& groups)
    {
        Json::Value res(Json::arrayValue);
        uint32_t i = 1;
        auto grIt = groups.begin();
        auto notGrIt = channelsNotInGroups.begin();
        while (grIt != groups.end() && notGrIt != channelsNotInGroups.end()) {
            if (grIt->Order <= i) {
                auto& item = Append(res);
                item["name"] = grIt->Name;
                item["device_type"] = grIt->Name;
                ++grIt;
            } else {
                res.append(**notGrIt);
                ++notGrIt;
            }
            ++i;
        }
        for (; grIt != groups.end(); ++grIt) {
            auto& item = Append(res);
            item["name"] = grIt->Name;
            item["device_type"] = grIt->Name;
        }
        for (; notGrIt != channelsNotInGroups.end(); ++notGrIt) {
            res.append(**notGrIt);
        }
        return res;
    }
}

Json::Value MakeSchemaForConfed(const Json::Value& configSchema,
                                TTemplateMap& templates,
                                TSerialDeviceFactory& deviceFactory)
{
    Json::Value res(configSchema);
    // Let's add to #/definitions/device/oneOf a list of devices generated from templates
    if (res["definitions"]["device"].isMember("oneOf")) {
        Json::Value newArray(Json::arrayValue);
        AppendDeviceSchemas(newArray, res["definitions"], res["translations"], templates, deviceFactory);
        for (auto& item: res["definitions"]["device"]["oneOf"]) {
            newArray.append(item);
        }
        res["definitions"]["device"]["oneOf"] = newArray;
    }
    res.setComment(std::string("// THIS FILE IS GENERATED BY wb-mqtt-serial SERVICE. DO NOT EDIT IT!"),
                   Json::commentBefore);
    return res;
}

void TransformGroupsToSubdevices(Json::Value& schema, Json::Value& subdevices)
{
    if (!schema.isMember("groups")) {
        return;
    }

    std::unordered_map<std::string, Json::Value> subdevicesForGroups; // key - group id
    std::vector<TGroup> groups;
    for (const auto& group: schema["groups"]) {
        Json::Value subdevice;
        subdevice["title"] = group["title"];
        subdevice["device_type"] = group["title"];
        subdevicesForGroups.emplace(group["id"].asString(), subdevice);
        groups.emplace_back(group);
    }

    std::stable_sort(groups.begin(), groups.end(), [](const auto& v1, const auto& v2) { return v1.Order < v2.Order; });

    auto channelsNotInGroups(PartitionChannelsByGroups(schema, subdevicesForGroups));
    auto newChannels(MergeChannels(channelsNotInGroups, groups));
    if (newChannels.empty()) {
        schema.removeMember("channels");
    } else {
        schema["channels"].swap(newChannels);
    }

    auto movedParameters(PartitionParametersByGroups(schema, subdevicesForGroups));
    for (const auto& param: movedParameters) {
        schema["parameters"].removeMember(param);
    }

    for (const auto& subdevice: subdevicesForGroups) {
        subdevices.append(subdevice.second);
    }
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

bool IsRequiredSetupRegister(const Json::Value& setupRegister)
{
    return setupRegister.get("required", false).asBool();
}
