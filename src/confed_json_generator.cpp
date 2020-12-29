#include "confed_json_generator.h"
#include "confed_schema_generator.h"
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
    Json::Value res;
    res["name"] = channelConfig["name"];
    if (channelConfig.isMember("device_type")) {
        res["device_type"] = channelConfig["device_type"];
        SetIfExists(res, "channels", channelConfig, "channels");
        SetIfExists(res, "setup", channelConfig, "setup");
        return res;
    }

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

    for (auto ch: regs) {
        customChannels.append(ch.second);
    }

    return std::make_pair(channels, customChannels);
}

std::pair<Json::Value, Json::Value> SplitChannels(const Json::Value& device, TTemplateMap& templates)
{
    if (!device.isMember("device_type")) {
        return std::make_pair(Json::Value(Json::arrayValue), device["channels"]);
    }

    return SplitChannels(device, templates.GetTemplate(device["device_type"].asString()));
}

bool IsCustomisableRegister(const Json::Value& registerTemplate)
{
    return !registerTemplate.isMember("value");
}

std::pair<Json::Value, Json::Value> SplitSetupRegisters(const Json::Value& configSchema, const Json::Value& deviceTemplateSchema)
{
    Json::Value setupParams;
    Json::Value customSetupRegisters(Json::arrayValue);
    std::unordered_map<std::string, Json::Value> configRegisters;
    if (configSchema.isMember("setup")) {
        for (const auto& r: configSchema["setup"]) {
            configRegisters.insert({r["title"].asString(), r});
        }
    }

    if (deviceTemplateSchema.isMember("setup")) {
        size_t i = 0;
        for (const auto& r: deviceTemplateSchema["setup"]) {
            if (IsCustomisableRegister(r)) {
                auto it = configRegisters.find(r["title"].asString());
                if (it != configRegisters.end()) {
                    setupParams[MakeSetupRegisterParameterName(i)] = it->second["value"];
                    configRegisters.erase(it);
                } else {
                    setupParams[MakeSetupRegisterParameterName(i)] = GetDefaultSetupRegisterValue(r);
                }
                ++i;
            }
        }
    }

    // Let's append custom registers in the order they are declared in config file
    if (!configRegisters.empty()) {
        for (const auto& r: configSchema["setup"]) {
            auto it = configRegisters.find(r["title"].asString());
            if (it != configRegisters.end()) {
                customSetupRegisters.append(r);
            }
        }
    }

    return std::make_pair(setupParams, customSetupRegisters);
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
//      "setup": [ ... ],
//      "channels": [ ... ]
//  }
//           ||
//           \/
//  {
//      "s_DT_HASH": {
//          "name": ...,
//          COMMON_DEVICE_SETUP_PARAMS,
//          "standard_setup": {
//              "set_1": { ... },
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
//      "setup": [ ... ],
//      "channels": [ ... ]
//  }
//           ||
//           \/
//  {
//      "name": ...,
//      "s_CT_HASH": {
//          "set_1": { ... },
//          ...
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

    auto setupRegs = SplitSetupRegisters(config, deviceTemplate);
    if (setupRegs.second.empty()) {
        newDev.removeMember("setup");
    } else {
        newDev["setup"] = setupRegs.second;
    }

    if (!setupRegs.first.empty()) {
        if (nestingLevel == 1) {
            AppendParams(newDev["standard_setup"], setupRegs.first);
        } else {
            AppendParams(newDev, setupRegs.first);
        }
    }

    newDev.removeMember("device_type");

    Json::Value customChannels;
    Json::Value standardChannels;
    std::tie(standardChannels, customChannels) = SplitChannels(config, deviceTemplate);
    if ( customChannels.empty() ) {
        newDev.removeMember("channels");
    } else {
        newDev["channels"] = customChannels;
    }
    if ( !standardChannels.empty() ) {
        if (nestingLevel == 1) {
            TSubDevicesTemplateMap templates(deviceTemplate);
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

Json::Value MakeJsonForConfed(const std::string& configFileName, const Json::Value& configSchema, TTemplateMap& templates)
{
    Json::Value root(Parse(configFileName));
    Validate(root, configSchema);
    for (Json::Value& port : root["ports"]) {
        for (Json::Value& device : port["devices"]) {
            if (!device.isMember("device_type")) {
                device["device_type"] = CUSTOM_DEVICE_TYPE;
            }
            device = MakeDeviceForConfed(device, templates, 1);
        }
    }

    return root;
}