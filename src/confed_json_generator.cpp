#include "confed_json_generator.h"
#include "confed_schema_generator.h"
#include "config_schema_generator.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

Json::Value MakeJsonFromChannelTemplate(const Json::Value& channelTemplate)
{
    Json::Value res;
    res["name"] = channelTemplate["name"];

    if (channelTemplate.isMember("device_type")) {
        res["device_type"] = channelTemplate["device_type"];
        return res;
    }
    if (channelTemplate.isMember("oneOf")) {
        if (channelTemplate.isMember("default")) {
            res["device_type"] = channelTemplate["default"];
        } else {
            res["device_type"] = channelTemplate["oneOf"][0].asString();
        }
        return res;
    }
    res["poll_interval"] = 20;
    SetIfExists(res, "poll_interval", channelTemplate, "poll_interval");
    res["enabled"] = true;
    SetIfExists(res, "enabled", channelTemplate, "enabled");
    return res;
}

Json::Value MakeJsonFromChannelConfig(const Json::Value& channelConfig)
{
    if (channelConfig.isMember("device_type")) {
        return channelConfig;
    }

    Json::Value res;
    res["name"] = channelConfig["name"];
    res["poll_interval"] = 20;
    SetIfExists(res, "poll_interval", channelConfig, "poll_interval");
    res["enabled"] = true;
    SetIfExists(res, "enabled", channelConfig, "enabled");
    return res;
}

std::pair<Json::Value, Json::Value> SplitChannels(const Json::Value& device, const Json::Value& deviceTemplate)
{
    Json::Value channels(Json::arrayValue);
    Json::Value customChannels(Json::arrayValue);
    std::unordered_map<std::string, Json::Value> regs;
    if (device.isMember("channels")) {
        for (const Json::Value& channel: device["channels"]) {
            regs[channel["name"].asString()] = channel;
        }
    }

    if (deviceTemplate.isMember("channels")) {
        for (const Json::Value& ch: deviceTemplate["channels"]) {
            auto it = regs.find(ch["name"].asString());
            if (it != regs.end()) { // template overriding
                channels.append(MakeJsonFromChannelConfig(it->second));
                regs.erase(it);
            } else { // use template channel definition
                channels.append(MakeJsonFromChannelTemplate(ch));
            }
        }
    }

    // Preserve custom channels order
    for (const Json::Value& ch: device["channels"]) {
        auto it = regs.find(ch["name"].asString());
        if (it != regs.end()) {
            customChannels.append(it->second);
        }
    }

    return std::make_pair(channels, customChannels);
}

std::pair<Json::Value, Json::Value> SplitChannels(const Json::Value& device, TTemplateMap& templates)
{
    if (!device.isMember("device_type")) {
        return std::make_pair(Json::Value(Json::arrayValue), device["channels"]);
    }

    return SplitChannels(device, templates.GetTemplate(device["device_type"].asString()).Schema);
}

bool IsCustomisableRegister(const Json::Value& registerTemplate)
{
    return !registerTemplate.isMember("value");
}

Json::Value GetSetupRegisters(const Json::Value& config, const Json::Value& deviceTemplateSchema)
{
    Json::Value setupRegisters;

    if (deviceTemplateSchema.isMember("parameters")) {
        for (auto it = deviceTemplateSchema["parameters"].begin(); it != deviceTemplateSchema["parameters"].end(); ++it) {
            if (config.isMember(it.name())) {
                setupRegisters[it.name()] = *it;
            }
        }
    }

    return setupRegisters;
}

Json::Value MakeDeviceForConfed(const Json::Value& config, ITemplateMap& deviceTemplates, size_t nestingLevel);

void MakeDevicesForConfed(Json::Value& devices, ITemplateMap& templates, size_t nestingLevel)
{
    for (Json::Value& device : devices) {
        if (device.isMember("device_type")) {
            device = MakeDeviceForConfed(device, templates, nestingLevel);
        }
    }
}

//  nestingLevel == 1
//  {
//      "name": ...
//      "device_type": DT,
//      COMMON_DEVICE_SETUP_PARAMS,
//      "parameter1": PARAMETER1_VALUE,
//      ...,
//      "setup": [ ... ],
//      "channels": [ ... ]
//  }
//           ||
//           \/
//  {
//      "s_DT_HASH": {
//          "name": ...,
//          COMMON_DEVICE_SETUP_PARAMS,
//          "parameters": {
//              "parameter1": PARAMETER1_VALUE,
//              ...
//          },
//          "setup": [ ... ],
//          "channels": [ ... ],
//          "standard_channels": [ ... ]
//      }
//  }

//  nestingLevel > 1
//  {
//      "name": ...
//      "device_type": CT,
//      "parameter1": PARAMETER1_VALUE,
//      ...,
//      "setup": [ ... ],
//      "channels": [ ... ]
//  }
//           ||
//           \/
//  {
//      "name": ...,
//      "s_CT_HASH": {
//          "parameter1": PARAMETER1_VALUE,
//          ...,
//          "setup": [ ... ],
//          "channels": [ ... ],
//          "standard_channels": [ ... ]
//      }
//  }

Json::Value MakeDeviceForConfed(const Json::Value& config, ITemplateMap& deviceTemplates, size_t nestingLevel)
{
    Json::Value ar(Json::arrayValue);
    if (nestingLevel > 5){
        LOG(Warn) << "Too deep nesting";
        return Json::Value();
    }

    auto dt = config["device_type"].asString();
    auto deviceTemplate = deviceTemplates.GetTemplate(dt);
    Json::Value newDev(config);

    if (nestingLevel == 1) {
        if (!newDev.isMember("slave_id") || (newDev["slave_id"].isString() && newDev["slave_id"].asString().empty())) {
            newDev["slave_id"] = false;
        }
    }

    if ((nestingLevel == 1) && (deviceTemplate.Schema.isMember("parameters"))) {
        for (auto it = deviceTemplate.Schema["parameters"].begin(); it != deviceTemplate.Schema["parameters"].end(); ++it) {
            if (newDev.isMember(it.name())) {
                newDev["parameters"][it.name()] = newDev[it.name()];
                newDev.removeMember(it.name());
            }
        }
    }

    newDev.removeMember("device_type");

    Json::Value customChannels;
    Json::Value standardChannels;
    std::tie(standardChannels, customChannels) = SplitChannels(config, deviceTemplate.Schema);
    newDev.removeMember("channels");
    if (!customChannels.empty()) {
        newDev["channels"] = customChannels;
    }
    if (!standardChannels.empty()) {
        if (nestingLevel == 1) {
            TSubDevicesTemplateMap templates(deviceTemplate.Type, deviceTemplate.Schema);
            MakeDevicesForConfed(standardChannels, templates, nestingLevel + 1);
        } else {
            MakeDevicesForConfed(standardChannels, deviceTemplates, nestingLevel + 1);
        } 
        newDev["standard_channels"] = standardChannels;
    }

    Json::Value res;

    if (nestingLevel > 1) {
        if (newDev.isMember("name")) {
            res["name"] = newDev["name"];
            newDev.removeMember("name");
        }
    }

    if (nestingLevel == 1) {
        res[GetDeviceKey(dt)] = newDev;
    } else {
        res[GetSubdeviceKey(dt)] = newDev;
    }
    return res;
}

Json::Value MakeJsonForConfed(const std::string&    configFileName,
                              const Json::Value&    baseConfigSchema,
                              TTemplateMap&         templates,
                              TSerialDeviceFactory& deviceFactory)
{
    Json::Value root(Parse(configFileName));
    auto configSchema = MakeSchemaForConfigValidation(baseConfigSchema,
                                                      GetValidationDeviceTypes(root),
                                                      templates,
                                                      deviceFactory);
    Validate(root, configSchema);
    for (Json::Value& port : root["ports"]) {
        for (Json::Value& device : port["devices"]) {
            if (device.isMember("device_type")) {
                device = MakeDeviceForConfed(device, templates, 1);
            }
        }
    }

    return root;
}