#include "confed_config_generator.h"
#include "confed_json_generator.h"
#include "confed_schema_generator.h"

using namespace std;
using namespace WBMQTT::JSON;

Json::Value FilterStandardChannels(const Json::Value& device,
                                   const Json::Value& deviceTemplate,
                                   ITemplateMap& templates,
                                   const std::unordered_map<std::string, std::string>& subdeviceTypeHashes);
void ExpandGroupChannels(Json::Value& device, const Json::Value& deviceTemplate);

bool RemoveDeviceHash(Json::Value& device, const std::unordered_map<std::string, std::string>& deviceTypeHashes)
{
    for (auto paramIt = device.begin(); paramIt != device.end(); ++paramIt) {
        auto hashedName = paramIt.name();
        if (deviceTypeHashes.count(hashedName)) {
            AppendParams(device, device[hashedName]);
            device.removeMember(hashedName);
            device["device_type"] = deviceTypeHashes.at(hashedName);
            return true;
        }
    }
    return false;
}

bool TryToTransformSubDeviceChannel(Json::Value& channel,
                                    ITemplateMap& templates,
                                    const std::unordered_map<std::string, std::string>& subdeviceTypeHashes)
{
    if (!RemoveDeviceHash(channel, subdeviceTypeHashes)) {
        return false;
    }

    Json::Value deviceTemplate(templates.GetTemplate(channel["device_type"].asString()).Schema);
    Json::Value filteredChannels(FilterStandardChannels(channel, deviceTemplate, templates, subdeviceTypeHashes));
    channel.removeMember("standard_channels");
    if (channel.isMember("channels")) {
        for (Json::Value& ch: filteredChannels) {
            channel["channels"].append(ch);
        }
    } else {
        channel["channels"].swap(filteredChannels);
    }

    ExpandGroupChannels(channel, deviceTemplate);

    if (channel["channels"].empty()) {
        channel.removeMember("channels");
    }

    return true;
}

bool TryToTransformSimpleChannel(Json::Value& ch, const Json::Value& channelTemplate)
{
    bool ok = false;
    if (ch.isMember("poll_interval")) {
        if (!channelTemplate.isMember("poll_interval") ||
            (ch["poll_interval"].asInt() != channelTemplate["poll_interval"].asInt()))
        {
            ok = true;
        }
    }
    if (ch.isMember("enabled")) {
        bool enabledInTemplate = true;
        Get(channelTemplate, "enabled", enabledInTemplate);
        if (ch["enabled"].asBool() == enabledInTemplate) {
            ch.removeMember("enabled");
        } else {
            ok = true;
        }
    }
    return ok;
}

Json::Value FilterStandardChannels(const Json::Value& device,
                                   const Json::Value& deviceTemplate,
                                   ITemplateMap& templates,
                                   const std::unordered_map<std::string, std::string>& subdeviceTypeHashes)
{
    Json::Value channels(Json::arrayValue);
    if (!device.isMember("standard_channels") || !deviceTemplate.isMember("channels")) {
        return channels;
    }

    std::unordered_map<std::string, Json::Value> regs;
    for (const Json::Value& channel: deviceTemplate["channels"]) {
        regs[channel["name"].asString()] = channel;
    }

    for (Json::Value ch: device["standard_channels"]) {
        auto it = regs.find(ch["name"].asString());
        if (it != regs.end()) {
            if (TryToTransformSubDeviceChannel(ch, templates, subdeviceTypeHashes) ||
                TryToTransformSimpleChannel(ch, it->second)) {
                channels.append(ch);
            }
        }
    }

    return channels;
}

// Move parameters and channels from channels representing groups to device's root
void ExpandGroupChannels(Json::Value& device, const Json::Value& deviceTemplate)
{
    if (!deviceTemplate.isMember("groups")) {
        return;
    }
    std::unordered_set<std::string> groups;
    for (const auto& group: deviceTemplate["groups"]) {
        groups.emplace(group["id"].asString());
    }

    Json::Value newChannels(Json::arrayValue);
    for (const auto& channel: device["channels"]) {
        if (groups.count(channel["name"].asString())) {
            for (const auto& subChannel: channel["channels"]) {
                newChannels.append(subChannel);
            }
            const std::unordered_set<std::string> notParameters{"channels", "name", "device_type"};
            for (auto it = channel.begin(); it != channel.end(); ++it) {
                if (!notParameters.count(it.name())) {
                    device[it.name()] = *it;
                }
            }
        } else {
            newChannels.append(channel);
        }
    }
    device["channels"].swap(newChannels);
}

Json::Value MakeConfigFromConfed(std::istream& stream, TTemplateMap& templates)
{
    Json::Value root;
    Json::CharReaderBuilder readerBuilder;
    Json::String errs;

    if (!Json::parseFromStream(readerBuilder, stream, &root, &errs)) {
        throw std::runtime_error("Failed to parse JSON:" + errs);
    }

    for (Json::Value& port: root["ports"]) {
        for (Json::Value& device: port["devices"]) {
            if (device.isMember("device_type")) {
                auto dt = device["device_type"].asString();

                if (dt == "unknown") {
                    auto v = device["value"];
                    device.removeMember("device_type");
                    device.removeMember("value");
                    device = v;
                } else {
                    Json::Value deviceTemplate(templates.GetTemplate(dt).Schema);
                    Json::Value subdevicesFromGroups(Json::arrayValue);
                    for (auto& subdeviceSchema: deviceTemplate["subdevices"]) {
                        TransformGroupsToSubdevices(subdeviceSchema["device"], subdevicesFromGroups);
                    }
                    for (auto& subdeviceSchema: subdevicesFromGroups) {
                        deviceTemplate["subdevices"].append(subdeviceSchema);
                    }
                    TransformGroupsToSubdevices(deviceTemplate, deviceTemplate["subdevices"]);
                    TSubDevicesTemplateMap subdevices(dt, deviceTemplate);
                    std::unordered_map<std::string, std::string> subdeviceTypeHashes;
                    for (const auto& dt: subdevices.GetDeviceTypes()) {
                        subdeviceTypeHashes[GetSubdeviceKey(dt)] = dt;
                    }

                    Json::Value filteredChannels(
                        FilterStandardChannels(device, deviceTemplate, subdevices, subdeviceTypeHashes));
                    device.removeMember("standard_channels");
                    for (Json::Value& ch: device["channels"]) {
                        filteredChannels.append(ch);
                    }
                    device["channels"].swap(filteredChannels);

                    ExpandGroupChannels(device, deviceTemplate);
                    if (device["channels"].empty()) {
                        device.removeMember("channels");
                    }
                }
            }

            AppendParams(device, device["parameters"]);
            device.removeMember("parameters");

            if (device["slave_id"].isBool()) {
                device.removeMember("slave_id");
            }
        }
    }

    return root;
}
