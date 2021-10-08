#include "confed_json_generator.h"
#include "confed_schema_generator.h"
#include "config_schema_generator.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

namespace
{
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
        SetIfExists(res, "poll_interval", channelConfig, "poll_interval");
        res["enabled"] = true;
        SetIfExists(res, "enabled", channelConfig, "enabled");
        return res;
    }

    std::pair<Json::Value, Json::Value> SplitChannels(const Json::Value& device, const Json::Value& deviceTemplate)
    {
        Json::Value                                  channels(Json::arrayValue);
        Json::Value                                  customChannels(Json::arrayValue);
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

    Json::Value MakeDeviceForConfed(const Json::Value& config, ITemplateMap& deviceTemplates, bool isSubdevice);

    void MakeDevicesForConfed(Json::Value& devices, ITemplateMap& templates, bool isSubdevice)
    {
        for (Json::Value& device: devices) {
            if (device.isMember("device_type")) {
                device = MakeDeviceForConfed(device, templates, isSubdevice);
            }
        }
    }

    Json::Value PartitionChannelsByGroups(const Json::Value&                            config,
                                          const Json::Value&                            schema,
                                          std::unordered_map<std::string, Json::Value>& channelsFromGroups)
    {
        std::unordered_map<std::string, Json::Value*> channelsToGroups;
        for (const auto& channel: schema["channels"]) {
            try {
                channelsToGroups.emplace(channel["name"].asString(),
                                         &channelsFromGroups.at(channel["group"].asString()));
            } catch (...) {
            }
        }
        Json::Value originalChannels(Json::arrayValue);
        for (const auto& channel: config["channels"]) {
            try {
                (*channelsToGroups.at(channel["name"].asString()))["channels"].append(channel);
            } catch (...) {
                originalChannels.append(channel);
            }
        }
        return originalChannels;
    }

    std::vector<std::string> PartitionParametersByGroups(
        const Json::Value& config,
        const Json::Value& schema,
        std::unordered_map<std::string, Json::Value>& channelsFromGroups)
    {
        std::unordered_map<std::string, Json::Value*> parametersToGroups;
        for (Json::ValueConstIterator it = schema["parameters"].begin(); it != schema["parameters"].end(); ++it) {
            try {
                parametersToGroups.emplace(it.name(), &channelsFromGroups.at((*it)["group"].asString()));
            } catch (...) {
            }
        }
        std::vector<std::string> movedParameters;
        for (Json::ValueConstIterator it = config.begin(); it != config.end(); ++it) {
            try {
                (*parametersToGroups.at(it.name()))[it.name()] = *it;
                movedParameters.emplace_back(it.name());
            } catch (...) {
            }
        }
        return movedParameters;
    }

    /**
     * @brief Creates channels for groups
     *        Moves parameters and channels to newly created channels according to group declaration
     */
    void JoinChannelsToGroups(Json::Value& config, const Json::Value& schema)
    {
        if (!schema.isMember("groups")) {
            return;
        }

        std::unordered_map<std::string, Json::Value> channelsFromGroups;
        for (const auto& group: schema["groups"]) {
            Json::Value item;
            item["name"]        = group["title"];
            item["device_type"] = group["title"];
            channelsFromGroups.emplace(group["id"].asString(), item);
        }

        auto movedParameters = PartitionParametersByGroups(config, schema, channelsFromGroups);
        for (const auto& param: movedParameters) {
            config.removeMember(param);
        }

        auto channelsNotInGroups = PartitionChannelsByGroups(config, schema, channelsFromGroups);
        for (const auto& channel: channelsFromGroups) {
            channelsNotInGroups.append(channel.second);
        }
        if (channelsNotInGroups.empty()) {
            config.removeMember("channels");
        } else {
            config["channels"].swap(channelsNotInGroups);
        }
    }

    //  Top level device
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

    //  Subdevice
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

    Json::Value MakeDeviceForConfed(const Json::Value& config, ITemplateMap& deviceTemplates, bool isSubdevice)
    {
        auto        dt             = config["device_type"].asString();
        auto        deviceTemplate = deviceTemplates.GetTemplate(dt);
        Json::Value schema(deviceTemplate.Schema);
        if (!schema.isMember("subdevices")) {
            schema["subdevices"] = Json::Value(Json::arrayValue);
        }

        Json::Value newDev(config);
        JoinChannelsToGroups(newDev, schema);
        newDev.removeMember("device_type");

        TransformGroupsToSubdevices(schema, schema["subdevices"]);

        Json::Value customChannels;
        Json::Value standardChannels;
        std::tie(standardChannels, customChannels) = SplitChannels(newDev, schema);
        newDev.removeMember("channels");
        if (!customChannels.empty()) {
            newDev["channels"] = customChannels;
        }
        if (!standardChannels.empty()) {
            if (isSubdevice) {
                MakeDevicesForConfed(standardChannels, deviceTemplates, true);
            } else {
                TSubDevicesTemplateMap subDeviceTemplates(deviceTemplate.Type, schema);
                MakeDevicesForConfed(standardChannels, subDeviceTemplates, true);
            }
            newDev["standard_channels"] = standardChannels;
        }

        Json::Value res;
        if (isSubdevice) {
            if (newDev.isMember("name")) {
                res["name"] = newDev["name"];
                newDev.removeMember("name");
            }
            res[GetSubdeviceKey(dt)] = newDev;
        } else {
            if (!newDev.isMember("slave_id") ||
                (newDev["slave_id"].isString() && newDev["slave_id"].asString().empty())) {
                newDev["slave_id"] = false;
            }
            if ((schema.isMember("parameters"))) {
                for (auto it = schema["parameters"].begin(); it != schema["parameters"].end(); ++it) {
                    if (newDev.isMember(it.name())) {
                        newDev["parameters"][it.name()] = newDev[it.name()];
                        newDev.removeMember(it.name());
                    }
                }
            }
            res[GetDeviceKey(dt)] = newDev;
        }
        return res;
    }
}

Json::Value MakeJsonForConfed(const std::string& configFileName,
                              const Json::Value& baseConfigSchema,
                              TTemplateMap& templates,
                              TSerialDeviceFactory& deviceFactory)
{
    Json::Value root(Parse(configFileName));
    auto configSchema =
        MakeSchemaForConfigValidation(baseConfigSchema, GetValidationDeviceTypes(root), templates, deviceFactory);
    Validate(root, configSchema);
    for (Json::Value& port: root["ports"]) {
        for (Json::Value& device: port["devices"]) {
            if (device.isMember("device_type")) {
                device = MakeDeviceForConfed(device, templates, false);
            }
        }
    }

    return root;
}