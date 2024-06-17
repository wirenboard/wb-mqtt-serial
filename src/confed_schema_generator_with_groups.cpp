#include "confed_schema_generator_with_groups.h"
#include "confed_channel_modes.h"
#include "confed_schema_generator.h"
#include "json_common.h"

#include <wblib/wbmqtt.h>

using namespace WBMQTT::JSON;

namespace
{
    const auto DEVICE_PARAMETERS_PROPERTY_ORDER = 20;

    //  {
    //      "type": "number", // or "type": "integer"
    //      "default": DEFAULT,
    //      "minimum": MIN,
    //      "maximum": MAX,
    //      "enum": [ ... ],
    //      "description": DESCRIPTION,
    //      "condition": CONDITION,
    //      "options": {
    //          "enumTitles" : [ ... ]
    //      }
    //  }
    Json::Value MakeParameterSchemaForOneOf(const Json::Value& setupRegister, int index)
    {
        Json::Value r;
        r["type"] = setupRegister.isMember("scale") ? "number" : "integer";
        r["default"] = GetDefaultSetupRegisterValue(setupRegister);
        SetIfExists(r, "enum", setupRegister, "enum");
        SetIfExists(r, "minimum", setupRegister, "min");
        SetIfExists(r, "maximum", setupRegister, "max");
        SetIfExists(r, "description", setupRegister, "description");
        if (setupRegister.isMember("enum_titles")) {
            r["options"]["enum_titles"] = setupRegister["enum_titles"];
        }
        SetIfExists(r, "condition", setupRegister, "condition");
        return r;
    }

    //  {
    //      "type": "number", // or "type": "integer"
    //      "title": TITLE,
    //      "default": DEFAULT,
    //      "minimum": MIN,
    //      "maximum": MAX,
    //      "enum": [ ... ],
    //      "description": DESCRIPTION,
    //      "propertyOrder": INDEX
    //      "group": GROUP_NAME,
    //      "condition": CONDITION,
    //      "requiredProp": REQUIRED      // if required
    //      "options": {
    //          "enumTitles" : [ ... ]
    //          "show_opt_in": true       // if not required
    //      }
    //  }
    Json::Value MakeParameterSchema(const Json::Value& setupRegister, int index)
    {
        Json::Value r(MakeParameterSchemaForOneOf(setupRegister, index));
        r["title"] = setupRegister["title"];
        r["propertyOrder"] = index;
        SetIfExists(r, "group", setupRegister, "group");
        if (!IsRequiredSetupRegister(setupRegister)) {
            r["options"]["show_opt_in"] = true;
        } else {
            // Use custom requiredProp to not confuse json-editor's validator as it can't handle conditions
            r["requiredProp"] = true;
        }
        return r;
    }

    void SetAndRemoveIfExists(Json::Value& dst, Json::Value& src, const std::string& key)
    {
        SetIfExists(dst, key, src, key);
        src.removeMember(key);
    }

    void ConvertToOneOfParameter(Json::Value& prop)
    {
        Json::Value newProp;
        SetAndRemoveIfExists(newProp, prop, "group");
        SetAndRemoveIfExists(newProp, prop, "propertyOrder");
        SetAndRemoveIfExists(newProp, prop, "title");
        SetAndRemoveIfExists(newProp, prop, "requiredProp");
        if (prop.isMember("options") && prop["options"].isMember("show_opt_in")) {
            SetAndRemoveIfExists(newProp["options"], prop["options"], "show_opt_in");
            if (prop["options"].empty()) {
                prop.removeMember("options");
            }
        }
        MakeArray("oneOf", newProp).append(prop);
        prop = newProp;
    }

    int AddDeviceParametersUI(Json::Value& properties, const Json::Value& deviceTemplate, int firstParameterOrder)
    {
        int maxOrder = 0;
        if (deviceTemplate.isMember("parameters") && !deviceTemplate["parameters"].empty()) {
            const auto& params = deviceTemplate["parameters"];
            int n = 0;
            for (Json::ValueConstIterator it = params.begin(); it != params.end(); ++it) {
                int order = it->get("order", n).asInt();
                maxOrder = std::max(order, maxOrder);
                auto& prop = properties[params.isArray() ? (*it)["id"].asString() : it.name()];
                if (prop.empty()) {
                    prop = MakeParameterSchema(*it, firstParameterOrder + order);
                } else {
                    if (!prop.isMember("oneOf")) {
                        ConvertToOneOfParameter(prop);
                    }
                    prop["oneOf"].append(MakeParameterSchemaForOneOf(*it, firstParameterOrder + order));
                }
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
    //      "title": CHANNEL_NAME,
    //      "group" : GROUP,
    //      "condition": CONDITION
    // }
    Json::Value MakeChannelSchema(const Json::Value& channelTemplate, int order)
    {
        Json::Value r;
        MakeArray("allOf", r).append(MakeObject("$ref", "#/definitions/groupsChannel"));
        r["default"] = ConfigToHomeuiGroupChannel(channelTemplate, order);
        r["title"] = channelTemplate["name"];
        SetIfExists(r, "group", channelTemplate, "group");
        SetIfExists(r, "condition", channelTemplate, "condition");
        return r;
    }

    void AddChannelsUI(Json::Value& properties, const Json::Value& deviceTemplate)
    {
        if (!deviceTemplate.isMember("channels")) {
            return;
        }
        const auto& channels = deviceTemplate["channels"];
        size_t n = 0;
        for (Json::ValueConstIterator it = channels.begin(); it != channels.end(); ++it) {
            properties[GetChannelPropertyName(n)] = MakeChannelSchema(*it, n);
            ++n;
        }
    }

    Json::Value MakeGroups(const Json::Value& deviceTemplate)
    {
        Json::Value res(Json::arrayValue);
        for (const Json::Value& group: deviceTemplate["groups"]) {
            Json::Value gr(group);
            gr["title"] = group["title"];
            if (group.isMember("description")) {
                gr["description"] = group["description"];
            }
            res.append(gr);
        }
        return res;
    }

    void AppendTranslations(Json::Value& dst, const Json::Value& src)
    {
        for (auto it = src.begin(); it != src.end(); ++it) {
            for (auto msgIt = it->begin(); msgIt != it->end(); ++msgIt) {
                dst[it.name()][msgIt.name()] = *msgIt;
            }
        }
    }
}

//  {
//      "format": "groups",
//      "options": {
//          "wb": {
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
Json::Value MakeDeviceWithGroupsUISchema(TDeviceTemplate& deviceTemplate,
                                         TSerialDeviceFactory& deviceFactory,
                                         const Json::Value& commonDeviceSchema)
{
    if (deviceTemplate.WithSubdevices()) {
        return Json::Value();
    }

    auto protocol = GetProtocolName(deviceTemplate.GetTemplate());
    auto res = commonDeviceSchema;
    res["format"] = "groups";
    res["properties"]["device_type"] = MakeHiddenProperty(deviceTemplate.Type);

    if (!deviceFactory.GetProtocol(protocol)->SupportsBroadcast()) {
        res["required"].append("slave_id");
    }

    auto groups = MakeGroups(deviceTemplate.GetTemplate());
    if (!groups.empty()) {
        res["options"]["wb"]["groups"] = groups;
    }

    auto& allOf = MakeArray("allOf", res);
    Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(protocol);
    allOf.append(MakeProtocolProperty());

    AddDeviceParametersUI(res["properties"], deviceTemplate.GetTemplate(), DEVICE_PARAMETERS_PROPERTY_ORDER);
    AddChannelsUI(res["properties"], deviceTemplate.GetTemplate());
    AppendTranslations(res["translations"], deviceTemplate.GetTemplate()["translations"]);
    return res;
}

std::string GetChannelPropertyName(size_t index)
{
    return std::string("ch") + std::to_string(index);
}