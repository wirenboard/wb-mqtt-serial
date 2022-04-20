#include "confed_schema_generator.h"
#include "confed_channel_modes.h"
#include "confed_schema_generator_with_groups.h"
#include "json_common.h"
#include "log.h"

#include <wblib/wbmqtt.h>

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
    //          "grid_columns": 6
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
        r["options"]["grid_columns"] = 6;
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
    //        "read_rate_limit_ms": CHANNEL_READ_RATE_LIMIT
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
        // poll_interval is deprecated, but still read it
        SetIfExists(r["default"], "read_rate_limit_ms", channelTemplate, "poll_interval");
        SetIfExists(r["default"], "read_rate_limit_ms", channelTemplate, "read_rate_limit_ms");
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
    Json::Value MakeTabSingleDeviceChannelSchema(const Json::Value& channel, TContext& context, bool disableTitle)
    {
        Json::Value r;
        r["headerTemplate"] = context.AddHashedTranslation(channel["name"].asString());
        if (channel.isMember("ui_options")) {
            r["options"] = channel["ui_options"];
        }
        if (disableTitle) {
            r["options"]["wb"]["disable_title"] = true;
        }
        auto& allOf = MakeArray("allOf", r);
        Append(allOf)["$ref"] =
            "#/definitions/" + GetSubdeviceSchemaKey(context.DeviceType, channel["device_type"].asString());
        allOf.append(MakeHiddenNameObject(channel["name"].asString()));
        return r;
    }

    Json::Value MakeTabChannelSchema(const Json::Value& channel, TContext& context, bool inTabsContainer)
    {
        if (channel.isMember("oneOf")) {
            return MakeTabOneOfChannelSchema(channel, context);
        }
        if (channel.isMember("device_type")) {
            return MakeTabSingleDeviceChannelSchema(channel, context, inTabsContainer);
        }
        return MakeTabSimpleChannelSchema(channel, context);
    }

    Json::Value MakeSimpleChannelData(const Json::Value& channel, TContext& context)
    {
        Json::Value v = ConfigToHomeuiSubdeviceChannel(channel);
        v["title"] = context.AddHashedTranslation(channel["name"].asString());
        return v;
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
    //              "read_rate_limit_ms": CHANNEL_READ_RATE_LIMIT,
    //              "read_period_ms": CHANNEL_READ_PERIOD
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
            r["options"]["disable_array_delete"] = true;
        }
        r["propertyOrder"] = propertyOrder;

        std::string format(deviceTemplate["ui_options"].get("channels_format", "default").asString());
        bool tabs = deviceTemplate.isMember("groups");
        if (format == "default") {
            tabs = std::any_of(channels.begin(), channels.end(), IsSubdeviceChannel);
        } else {
            tabs = true;
        }

        if (tabs) {
            auto& items = MakeArray("items", r);
            for (const auto& channel: channels) {
                items.append(MakeTabChannelSchema(channel, context, format == "default"));
            }
            r["minItems"] = items.size();
            r["maxItems"] = items.size();
        } else {
            r["options"]["grid_columns"] = MAX_GRID_COLUMNS;
            r["items"]["$ref"] = "#/definitions/tableChannelSettings";
            auto& defaults = MakeArray("default", r);
            for (const auto& channel: channels) {
                defaults.append(MakeSimpleChannelData(channel, context));
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
    //      "options": {
    //          "disable_edit_json": true,
    //          "compact": true,
    //          "wb": {
    //              "disable_title": true,
    //              "hide_from_selection": true // if deprecated
    //          }
    //      },
    //      "allOf": [
    //          { "$ref": PROTOCOL_PARAMETERS },
    //          { HIDDEN_PROTOCOL }
    //      ],
    //      "properties": {
    //          "device_type": DEVICE_TYPE,
    //          "standard_channels": STANDARD_CHANNELS_SCHEMA,
    //          "parameters": PARAMETERS_SCHEMA
    //      },
    //      "defaultProperties": ["device_type", "slave_id", "standard_channels", "parameters"],
    //      "required": ["device_type", "slave_id"]
    //  }
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
        Json::Value subdevicesFromGroups(Json::arrayValue);
        for (auto& subdeviceSchema: schema["subdevices"]) {
            TransformGroupsToSubdevices(subdeviceSchema["device"], subdevicesFromGroups, &schema["translations"]);
        }
        for (auto& subdeviceSchema: subdevicesFromGroups) {
            schema["subdevices"].append(subdeviceSchema);
        }
        TransformGroupsToSubdevices(schema, schema["subdevices"], &schema["translations"]);

        TContext context{deviceTemplate.Type, translations};
        auto& res = Append(devicesArray);

        res["type"] = "object";
        res["title"] = context.AddHashedTranslation(deviceTemplate.Title);
        res["properties"]["device_type"] = MakeHiddenProperty(deviceTemplate.Type);
        MakeArray("required", res).append("device_type");

        res["options"]["wb"]["disable_title"] = true;
        if (deviceTemplate.IsDeprecated) {
            res["options"]["wb"]["hide_from_selection"] = true;
        }

        auto& allOf = MakeArray("allOf", res);
        auto protocol = GetProtocolName(schema);
        Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(protocol);
        allOf.append(MakeProtocolProperty());

        if (!deviceFactory.GetProtocol(protocol)->SupportsBroadcast()) {
            res["required"].append("slave_id");
        }

        auto& defaultProperties = MakeArray("defaultProperties", res);
        defaultProperties.append("slave_id");
        defaultProperties.append("device_type");

        if (schema.isMember("channels")) {
            res["properties"]["standard_channels"] =
                MakeChannelsSchema(schema, DEVICE_PARAMETERS_PROPERTY_ORDER + 1, "Channels", context, true);
            defaultProperties.append("standard_channels");
        }
        if (schema.isMember("parameters") && !schema["parameters"].empty()) {
            res["properties"]["parameters"] = MakeDeviceSettingsUI(schema, DEVICE_PARAMETERS_PROPERTY_ORDER, context);
            defaultProperties.append("parameters");
        }

        TSubDevicesTemplateMap subdeviceTemplates(deviceTemplate.Type, schema);
        if (schema.isMember("subdevices")) {
            for (const auto& subDevice: schema["subdevices"]) {
                auto name = subDevice["device_type"].asString();
                MakeSubDeviceUISchema(name, subdeviceTemplates.GetTemplate(name), definitions, context);
            }
        }
        AddTranslations(deviceTemplate.Type, translations, schema);
    }

    void AppendDeviceSchemas(Json::Value& devicesArray,
                             Json::Value& definitions,
                             Json::Value& translations,
                             TTemplateMap& templates,
                             TSerialDeviceFactory& deviceFactory)
    {
        for (const auto& t: templates.GetTemplatesOrderedByName()) {
            try {
                if (t->Schema.isMember("subdevices")) {
                    AddDeviceUISchema(*t, deviceFactory, devicesArray, definitions, translations);
                } else {
                    AddDeviceWithGroupsUISchema(*t, deviceFactory, devicesArray, definitions, translations);
                }
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
        std::string Id;
        Json::Value Options;

        TGroup(const Json::Value& group): Order(group.get("order", 1).asUInt()), Id(group["id"].asString())
        {
            if (group.isMember("ui_options")) {
                Options = group["ui_options"];
            }
        }
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
                item["name"] = grIt->Id;
                item["device_type"] = grIt->Id;
                if (!grIt->Options.empty()) {
                    item["ui_options"] = grIt->Options;
                }
                ++grIt;
            } else {
                res.append(**notGrIt);
                ++notGrIt;
            }
            ++i;
        }
        for (; grIt != groups.end(); ++grIt) {
            auto& item = Append(res);
            item["name"] = grIt->Id;
            item["device_type"] = grIt->Id;
            if (!grIt->Options.empty()) {
                item["ui_options"] = grIt->Options;
            }
        }
        for (; notGrIt != channelsNotInGroups.end(); ++notGrIt) {
            res.append(**notGrIt);
        }
        return res;
    }
}

void AddUnitTypes(Json::Value& schema)
{
    auto& values = MakeArray("enum_values", schema["definitions"]["units"]["options"]);
    for (const auto& unit: WBMQTT::TControl::GetUnitTypes()) {
        values.append(unit);
    }
}

Json::Value MakeSchemaForConfed(const Json::Value& configSchema,
                                TTemplateMap& templates,
                                TSerialDeviceFactory& deviceFactory)
{
    Json::Value res(configSchema);

    // Old configs could have slave_id defined as number not as string.
    // To not confuse users convert numbers to strings and show only string editor for slave_id.
    // Original slave_id definitions with numbers are used during config validation.
    res["definitions"]["slave_id"] = res["definitions"]["slave_id_ui"];
    res["definitions"].removeMember("slave_id_ui");
    res["definitions"]["slave_id_broadcast"] = res["definitions"]["slave_id_broadcast_ui"];
    res["definitions"].removeMember("slave_id_broadcast_ui");

    AddUnitTypes(res);
    AddChannelModes(res["definitions"]["groupsChannel"]);
    AddChannelModes(res["definitions"]["tableChannelSettings"]);

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

void AddGroupTitleTranslations(const std::string& id, const std::string& title, Json::Value& mainSchemaTranslations)
{
    if (!mainSchemaTranslations.isMember("en")) {
        mainSchemaTranslations["en"] = Json::Value(Json::objectValue);
    }

    for (auto langIt = mainSchemaTranslations.begin(); langIt != mainSchemaTranslations.end(); ++langIt) {
        if (langIt->isMember(title)) {
            (*langIt)[id] = (*langIt)[title];
        } else {
            if (langIt.name() == "en") {
                (*langIt)[id] = title;
            }
        }
    }
}

void TransformGroupsToSubdevices(Json::Value& schema, Json::Value& subdevices, Json::Value* mainSchemaTranslations)
{
    if (!schema.isMember("groups")) {
        return;
    }

    std::unordered_map<std::string, Json::Value> subdevicesForGroups; // key - group id
    std::vector<TGroup> groups;
    for (const auto& group: schema["groups"]) {
        auto id = group["id"].asString();
        Json::Value subdevice;
        subdevice["title"] = id;
        subdevice["device_type"] = id;
        subdevicesForGroups.emplace(id, subdevice);
        groups.emplace_back(group);
        if (mainSchemaTranslations) {
            auto title = group["title"].asString();
            AddGroupTitleTranslations(id, title, *mainSchemaTranslations);
        }
    }

    std::stable_sort(groups.begin(), groups.end(), [](const auto& v1, const auto& v2) { return v1.Order < v2.Order; });

    if (schema.isMember("channels")) {
        auto channelsNotInGroups(PartitionChannelsByGroups(schema, subdevicesForGroups));
        auto newChannels(MergeChannels(channelsNotInGroups, groups));
        schema["channels"].swap(newChannels);
        if (schema["channels"].empty()) {
            schema.removeMember("channels");
        }
    }

    if (schema.isMember("parameters")) {
        auto movedParameters(PartitionParametersByGroups(schema, subdevicesForGroups));
        for (const auto& param: movedParameters) {
            schema["parameters"].removeMember(param);
        }
        if (schema["parameters"].empty()) {
            schema.removeMember("parameters");
        }
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

std::string GetTranslationHash(const std::string& deviceType, const std::string& msg)
{
    return "t" + std::to_string(std::hash<std::string>()(deviceType + msg));
}

Json::Value MakeHiddenProperty(const std::string& value)
{
    Json::Value res;
    res["type"] = "string";
    res["default"] = value;
    MakeArray("enum", res).append(value);
    res["options"]["hidden"] = true;
    return res;
}

Json::Value MakeProtocolProperty()
{
    Json::Value r;
    r["properties"]["protocol"]["type"] = "string";
    r["properties"]["protocol"]["options"]["hidden"] = true;
    return r;
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
