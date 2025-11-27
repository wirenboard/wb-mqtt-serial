#include "confed_schema_generator.h"

#include <wblib/wbmqtt.h>

#include "json_common.h"

namespace
{
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
    //      "allOf": [
    //          { "$ref": PROTOCOL_PARAMETERS }
    //      ],
    //      "format": "groups",
    //      "device": DEVICE_TEMPLATE,
    //      "properties": {
    //          "device_type": DEVICE_TYPE
    //      },
    //      "required": ["device_type", "slave_id"]
    //  }
    Json::Value MakeDeviceWithGroupsTemplateSchema(TDeviceTemplate& deviceTemplate,
                                                   TSerialDeviceFactory& deviceFactory,
                                                   const Json::Value& commonDeviceSchema)
    {
        auto protocol = GetProtocolName(deviceTemplate.GetTemplate());
        auto res = commonDeviceSchema;
        if (!deviceFactory.GetProtocol(protocol)->SupportsBroadcast()) {
            res["required"].append("slave_id");
        }

        auto& allOf = MakeArray("allOf", res);
        Append(allOf)["$ref"] = deviceFactory.GetCommonDeviceSchemaRef(protocol);
        allOf.append(MakeProtocolProperty());

        res["format"] = "groups";
        res["device"] = deviceTemplate.GetTemplate();
        res["properties"]["device_type"] = MakeHiddenProperty(deviceTemplate.Type);
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

void AddTranslations(Json::Value& translations, const Json::Value& deviceSchema)
{
    const auto& tr = deviceSchema["translations"];
    for (auto it = tr.begin(); it != tr.end(); ++it) {
        for (auto msgIt = it->begin(); msgIt != it->end(); ++msgIt) {
            translations[it.name()][msgIt.name()] = *msgIt;
        }
    }
}

Json::Value GenerateSchemaForConfed(TDeviceTemplate& deviceTemplate,
                                    TSerialDeviceFactory& deviceFactory,
                                    const Json::Value& commonDeviceSchema)
{
    auto schema = deviceTemplate.WithSubdevices()
                      ? Json::Value(Json::objectValue)
                      : MakeDeviceWithGroupsTemplateSchema(deviceTemplate, deviceFactory, commonDeviceSchema);
    AddUnitTypes(schema);
    return schema;
}
