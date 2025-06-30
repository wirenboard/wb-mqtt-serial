#include "subdevices_config/confed_config_generator.h"
#include "subdevices_config/confed_channel_modes.h"
#include "subdevices_config/confed_schema_generator.h"

#include "json_common.h"
#include "templates_map.h"

using namespace std;
using namespace WBMQTT::JSON;

namespace
{
    Json::Value FilterStandardChannels(const Json::Value& device,
                                       const Json::Value& deviceTemplate,
                                       TSubDevicesTemplateMap& templates,
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
                                        TSubDevicesTemplateMap& templates,
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

    /**
     * @brief  Compare "read_rate_limit_ms" property with template.
     *         Remove it from ch if it is equal to specified in template.
     *
     * @return true channel has "read_rate_limit_ms" property different from template's value
     * @return false channel has "read_rate_limit_ms" property same as in template
     */
    bool TryToTransformReadRateLimit(Json::Value& ch, const Json::Value& channelTemplate)
    {
        if (!ch.isMember("read_rate_limit_ms")) {
            return false;
        }
        if (channelTemplate.isMember("read_rate_limit_ms") &&
            (ch["read_rate_limit_ms"].asInt() == channelTemplate["read_rate_limit_ms"].asInt()))
        {
            ch.removeMember("read_rate_limit_ms");
            return false;
        }
        return true;
    }

    /**
     * @brief  Compare parameters with template.
     *         Add channel to resulting config file if:
     *         - it has "read_rate_limit_ms" or "enabled" property
     *           different from that specified in template
     *         - it has "read_period_ms" property
     *         Remove properties equal to specified in template
     */
    bool TryToTransformSimpleChannel(Json::Value& ch, const Json::Value& channelTemplate)
    {
        ch = Subdevices::HomeuiToConfigChannel(ch);
        bool ok = TryToTransformReadRateLimit(ch, channelTemplate);
        if (ch.isMember("enabled")) {
            bool enabledInTemplate = true;
            Get(channelTemplate, "enabled", enabledInTemplate);
            if (ch["enabled"].asBool() == enabledInTemplate) {
                ch.removeMember("enabled");
            } else {
                ok = true;
            }
        }
        return ok || ch.isMember("read_period_ms");
    }

    Json::Value FilterStandardChannels(const Json::Value& device,
                                       const Json::Value& deviceTemplate,
                                       TSubDevicesTemplateMap& templates,
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
                    TryToTransformSimpleChannel(ch, it->second))
                {
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
}

void Subdevices::MakeDeviceConfigFromConfed(Json::Value& device,
                                            const std::string& deviceType,
                                            const Json::Value& deviceTemplate)
{
    Json::Value deviceTemplateCopy(deviceTemplate);
    Json::Value subdevicesFromGroups(Json::arrayValue);
    for (auto& subdeviceSchema: deviceTemplateCopy["subdevices"]) {
        Subdevices::TransformGroupsToSubdevices(subdeviceSchema["device"], subdevicesFromGroups);
    }
    for (auto& subdeviceSchema: subdevicesFromGroups) {
        deviceTemplateCopy["subdevices"].append(subdeviceSchema);
    }
    Subdevices::TransformGroupsToSubdevices(deviceTemplateCopy, deviceTemplateCopy["subdevices"]);
    TSubDevicesTemplateMap subdevices(deviceType, deviceTemplateCopy);
    std::unordered_map<std::string, std::string> subdeviceTypeHashes;
    for (const auto& dt: subdevices.GetDeviceTypes()) {
        subdeviceTypeHashes[Subdevices::GetSubdeviceKey(dt)] = dt;
    }

    Json::Value filteredChannels(FilterStandardChannels(device, deviceTemplateCopy, subdevices, subdeviceTypeHashes));
    device.removeMember("standard_channels");
    for (Json::Value& ch: device["channels"]) {
        filteredChannels.append(ch);
    }
    device["channels"].swap(filteredChannels);

    ExpandGroupChannels(device, deviceTemplateCopy);
    if (device["channels"].empty()) {
        device.removeMember("channels");
    }

    AppendParams(device, device["parameters"]);
    device.removeMember("parameters");
}
