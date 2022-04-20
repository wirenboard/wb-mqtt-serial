#include "confed_schema_generator_with_groups.h"
#include "confed_channel_modes.h"
#include "confed_schema_generator.h"
#include "json_common.h"

#include <wblib/wbmqtt.h>

using namespace WBMQTT::JSON;

namespace
{
    const auto DEVICE_PARAMETERS_PROPERTY_ORDER = 20;

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
    //      "type": "number", // or "type": "integer"
    //      "title": TITLE_HASH,
    //      "default": DEFAULT,
    //      "minimum": MIN,
    //      "maximum": MAX,
    //      "enum": [ ... ],
    //      "description": DESCRIPTION_HASH,
    //      "propertyOrder": INDEX
    //      "group": GROUP_NAME,
    //      "condition": CONDITION,
    //      "requiredProp": REQUIRED      // if required
    //      "options": {
    //          "enumTitles" : [ ... ]
    //          "show_opt_in": true       // if not required
    //      }
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
        SetIfExists(r, "group", setupRegister, "group");
        SetIfExists(r, "condition", setupRegister, "condition");
        if (!IsRequiredSetupRegister(setupRegister)) {
            r["options"]["show_opt_in"] = true;
        } else {
            // Use custom requiredProp to not confuse json-editor's validator as it can't handle conditions
            r["requiredProp"] = true;
        }
        return r;
    }

    int AddDeviceParametersUI(Json::Value& properties,
                              const Json::Value& deviceTemplate,
                              int firstParameterOrder,
                              TContext& context)
    {
        int maxOrder = 0;
        if (deviceTemplate.isMember("parameters") && !deviceTemplate["parameters"].empty()) {
            const auto& params = deviceTemplate["parameters"];
            int n = 0;
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                int order = it->get("order", n).asInt();
                maxOrder = std::max(order, maxOrder);
                properties[it.name()] = MakeParameterSchema(*it, firstParameterOrder + order, context);
                ++n;
            }
        }
        return firstParameterOrder + maxOrder + 1;
    }

    // {
    //      "allOf" : [
    //         {"$ref" : "#/definitions/groupsChannel"},
    //      ],
    //      "default" : DEFAULT_CHANNEL_VALUE,
    //      "title": CHANNEL_NAME_HASH,
    //      "group" : GROUP,
    //      "condition": CONDITION
    // }
    Json::Value MakeChannelSchema(const Json::Value& channelTemplate, int order, TContext& context)
    {
        Json::Value r;
        MakeArray("allOf", r).append(MakeObject("$ref", "#/definitions/groupsChannel"));
        r["default"] = ConfigToHomeuiGroupChannel(channelTemplate, order);
        r["title"] = context.AddHashedTranslation(channelTemplate["name"].asString());
        SetIfExists(r, "group", channelTemplate, "group");
        SetIfExists(r, "condition", channelTemplate, "condition");
        return r;
    }

    void AddChannelsUI(Json::Value& properties, const Json::Value& deviceTemplate, TContext& context)
    {
        if (!deviceTemplate.isMember("channels")) {
            return;
        }
        const auto& channels = deviceTemplate["channels"];
        size_t n = 0;
        for (Json::ValueConstIterator it = channels.begin(); it != channels.end(); ++it) {
            properties[GetChannelPropertyName(n)] = MakeChannelSchema(*it, n, context);
            ++n;
        }
    }

    Json::Value MakeGroups(const Json::Value& deviceTemplate, TContext& context)
    {
        Json::Value res(Json::arrayValue);
        for (const Json::Value& group: deviceTemplate["groups"]) {
            Json::Value gr(group);
            gr["title"] = context.AddHashedTranslation(group["title"].asString());
            res.append(gr);
        }
        return res;
    }
}

//  {
//      "type": "object",
//      "title": DEVICE_TITLE_HASH,
//      "_format": "groups",
//      "options": {
//          "disable_edit_json": true,
//          "compact": true,
//          "wb": {
//              "disable_title": true,
//              "hide_from_selection": true // if deprecated
//              "groups": GROUPS_SCHEMA
//          }
//      },
//      "allOf": [
//          { "$ref": PROTOCOL_PARAMETERS },
//          { HIDDEN_PROTOCOL }
//      ],
//      "properties": {
//          "device_type": DEVICE_TYPE,
//          STANDARD_CHANNELS_CONVERTED_TO_PARAMETERS_SCHEMAS,
//          PARAMETERS_SCHEMAS,
//      },
//      "defaultProperties": ["device_type", "slave_id"],
//      "required": ["device_type", "slave_id"]
//  }
void AddDeviceWithGroupsUISchema(const TDeviceTemplate& deviceTemplate,
                                 TSerialDeviceFactory& deviceFactory,
                                 Json::Value& devicesArray,
                                 Json::Value& definitions,
                                 Json::Value& translations)
{
    if (deviceTemplate.Schema.isMember("subdevices")) {
        return;
    }
    TContext context{deviceTemplate.Type, translations};
    auto& res = Append(devicesArray);

    res["type"] = "object";
    res["title"] = context.AddHashedTranslation(deviceTemplate.Title);
    res["_format"] = "groups";
    res["properties"]["device_type"] = MakeHiddenProperty(deviceTemplate.Type);
    MakeArray("required", res).append("device_type");
    res["required"].append("slave_id");

    res["options"]["wb"]["disable_title"] = true;
    if (deviceTemplate.IsDeprecated) {
        res["options"]["wb"]["hide_from_selection"] = true;
    }

    auto groups = MakeGroups(deviceTemplate.Schema, context);
    if (!groups.empty()) {
        res["options"]["wb"]["groups"] = groups;
    }

    auto& allOf = MakeArray("allOf", res);
    auto protocol = GetProtocolName(deviceTemplate.Schema);
    Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(protocol);
    allOf.append(MakeProtocolProperty());

    auto& defaultProperties = MakeArray("defaultProperties", res);
    defaultProperties.append("slave_id");
    defaultProperties.append("device_type");

    AddDeviceParametersUI(res["properties"], deviceTemplate.Schema, DEVICE_PARAMETERS_PROPERTY_ORDER, context);
    AddChannelsUI(res["properties"], deviceTemplate.Schema, context);
    AddTranslations(deviceTemplate.Type, translations, deviceTemplate.Schema);
}

std::string GetChannelPropertyName(size_t index)
{
    return std::string("ch") + std::to_string(index);
}