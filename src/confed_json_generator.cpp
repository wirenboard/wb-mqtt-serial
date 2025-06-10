#include "confed_json_generator.h"
#include "confed_channel_modes.h"
#include "confed_schema_generator.h"
#include "config_schema_generator.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

namespace
{
    // poll_interval is deprecated, we convert it to read_rate_limit_ms
    void ConvertPollIntervalToReadRateLimit(Json::Value& node)
    {
        if (node.isMember("poll_interval")) {
            if (!node.isMember("read_rate_limit_ms")) {
                node["read_rate_limit_ms"] = node["poll_interval"];
            }
            node.removeMember("poll_interval");
        }
    }

    Json::Value MakeJsonFromChannelTemplate(const Json::Value& channelTemplate)
    {
        if (channelTemplate.isMember("device_type")) {
            Json::Value res;
            res["name"] = channelTemplate["name"];
            res["device_type"] = channelTemplate["device_type"];
            return res;
        }
        if (channelTemplate.isMember("oneOf")) {
            Json::Value res;
            res["name"] = channelTemplate["name"];
            if (channelTemplate.isMember("default")) {
                res["device_type"] = channelTemplate["default"];
            } else {
                res["device_type"] = channelTemplate["oneOf"][0].asString();
            }
            return res;
        }
        return ConfigToHomeuiSubdeviceChannel(channelTemplate);
    }

    Json::Value MakeJsonFromChannelConfig(const Json::Value& channelConfig)
    {
        if (channelConfig.isMember("device_type")) {
            return channelConfig;
        }

        return ConfigToHomeuiSubdeviceChannel(channelConfig);
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
                // Some configs have settings for channels from old templates.
                // They don't have appropriate definitions and will be treated as custom channels without addresses.
                // It could lead to json-editor confusing and wrong web UI generation
                // Just drop such channels
                // TODO: It is not a good solution and should be deleted after template versions implementation
                if (it->second.isMember("address") || it->second.isMember("consists_of")) {
                    customChannels.append(it->second);
                }
            }
        }

        return std::make_pair(channels, customChannels);
    }

    Json::Value MakeSubDeviceForConfed(const Json::Value& config, TSubDevicesTemplateMap& subDeviceTemplates);

    void MakeSubDevicesForConfed(Json::Value& devices, TSubDevicesTemplateMap& subDeviceTemplates)
    {
        for (Json::Value& device: devices) {
            if (device.isMember("device_type")) {
                device = MakeSubDeviceForConfed(device, subDeviceTemplates);
            }
        }
    }

    Json::Value PartitionChannelsByGroups(const Json::Value& config,
                                          const Json::Value& schema,
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
            item["name"] = group["id"];
            item["device_type"] = group["id"];
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

    Json::Value MakeSubDeviceForConfed(const Json::Value& config, TSubDevicesTemplateMap& subDeviceTemplates)
    {
        auto dt = config["device_type"].asString();
        auto deviceTemplate = subDeviceTemplates.GetTemplate(dt);
        Json::Value schema(deviceTemplate.Schema);
        Json::Value newDev(config);
        JoinChannelsToGroups(newDev, schema);
        newDev.removeMember("device_type");

        auto newSubdevices = Json::Value(Json::arrayValue);
        TransformGroupsToSubdevices(schema, newSubdevices);
        subDeviceTemplates.AddSubdevices(newSubdevices);

        Json::Value customChannels;
        Json::Value standardChannels;
        std::tie(standardChannels, customChannels) = SplitChannels(newDev, schema);
        newDev.removeMember("channels");
        if (!customChannels.empty()) {
            newDev["channels"] = customChannels;
        }
        if (!standardChannels.empty()) {
            MakeSubDevicesForConfed(standardChannels, subDeviceTemplates);
            newDev["standard_channels"] = standardChannels;
        }

        Json::Value res;
        if (newDev.isMember("name")) {
            res["name"] = newDev["name"];
            newDev.removeMember("name");
        }
        res[GetSubdeviceKey(dt)] = newDev;
        return res;
    }

    void UpdateDeviceChannels(Json::Value& device, PDeviceTemplate deviceTemplate)
    {
        // Some configs have settings for channels from old templates or manually added channels.
        // Result may contain only manually added channels and channels declared in device template.
        Json::Value channels;
        for (const Json::Value& deviceChannel: device["channels"]) {
            if (deviceChannel.isMember("address") || deviceChannel.isMember("consists_of")) {
                channels.append(deviceChannel);
                continue;
            }
            for (const Json::Value& templateChannel: deviceTemplate->GetTemplate()["channels"]) {
                if (deviceChannel["name"] == templateChannel["name"]) {
                    channels.append(deviceChannel);
                    break;
                }
            }
        }
        device.removeMember("channels");
        if (!channels.empty()) {
            device["channels"] = channels;
        }
    }

    void UpdateSubDeviceChannels(Json::Value& device, PDeviceTemplate deviceTemplate)
    {
        Json::Value schema(deviceTemplate->GetTemplate());
        Json::Value customChannels;
        Json::Value standardChannels;
        JoinChannelsToGroups(device, schema);
        TransformGroupsToSubdevices(schema, schema["subdevices"]);
        std::tie(standardChannels, customChannels) = SplitChannels(device, schema);
        device.removeMember("channels");
        if (!customChannels.empty()) {
            device["channels"] = customChannels;
        }
        if (!standardChannels.empty()) {
            TSubDevicesTemplateMap subDeviceTemplates(deviceTemplate->Type, schema);
            MakeSubDevicesForConfed(standardChannels, subDeviceTemplates);
            device["standard_channels"] = standardChannels;
        }
        if (schema.isMember("parameters")) {
            for (auto it = schema["parameters"].begin(); it != schema["parameters"].end(); ++it) {
                if (device.isMember(it.name())) {
                    device["parameters"][it.name()] = device[it.name()];
                    device.removeMember(it.name());
                }
            }
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

    Json::Value MakeDeviceForConfed(const Json::Value& config, TTemplateMap& deviceTemplates)
    {
        PDeviceTemplate deviceTemplate;
        try {
            deviceTemplate = deviceTemplates.GetTemplate(config["device_type"].asString());
        } catch (...) {
            return config;
        }

        Json::Value newDev(config);
        ConvertPollIntervalToReadRateLimit(newDev);
        if (deviceTemplate->WithSubdevices()) {
            UpdateSubDeviceChannels(newDev, deviceTemplate);
        } else {
            UpdateDeviceChannels(newDev, deviceTemplate);
        }

        // Old configs could have slave_id defined as number not as string.
        // To not confuse users convert numbers to strings and show only string editor for slave_id.
        if (newDev.isMember("slave_id") && newDev["slave_id"].isNumeric()) {
            newDev["slave_id"] = newDev["slave_id"].asString();
        }
        if (!newDev.isMember("slave_id") || (newDev["slave_id"].isString() && newDev["slave_id"].asString().empty())) {
            newDev.removeMember("slave_id");
        }

        return newDev;
    }
}

Json::Value MakeJsonForConfed(const std::string& configFileName, TTemplateMap& templates)
{
    Json::Value root(Parse(configFileName));
    // If a file contains some symbols, but not a valid JSON,
    // jsoncpp can return Json::nullValue instead of throwing an error
    if (!root.isObject()) {
        throw std::runtime_error("Failed to parse " + configFileName + ". The file is not a valid JSON");
    }
    if (root.isMember("ports")) {
        for (Json::Value& port: root["ports"]) {
            ConvertPollIntervalToReadRateLimit(port);
            if (port.isMember("devices")) {
                for (Json::Value& device: port["devices"]) {
                    if (device.isMember("device_type")) {
                        device = MakeDeviceForConfed(device, templates);
                    }
                }
            }
        }
    }

    return root;
}