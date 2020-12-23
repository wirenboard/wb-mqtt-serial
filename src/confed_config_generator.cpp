#include "confed_config_generator.h"
#include "confed_json_generator.h"
#include "confed_schema_generator.h"

using namespace std;
using namespace WBMQTT::JSON;

Json::Value FilterStandardChannels(const Json::Value& device, 
                                   const Json::Value& deviceTemplate,
                                   ITemplateMap& templates,
                                   const std::unordered_map<std::string, std::string>& subdeviceTypeHashes);

void TransformSetupParams(Json::Value& device, const Json::Value& deviceTemplate)
{
    if (!deviceTemplate.isMember("setup")) {
        return;
    }
    if (!device.isMember("setup")) {
        device["setup"] = Json::Value(Json::arrayValue);
    }
    size_t i = 0;
    for (const auto& setupItem : deviceTemplate["setup"]) {
        if (!setupItem.isMember("value")) {
            auto paramName = MakeSetupRegisterParameterName(i);
            if (device.isMember(paramName)) {
                Json::Value item;
                item["title"] = setupItem["title"];
                item["value"] = device[paramName];
                device["setup"].append(item);
                device.removeMember(paramName);
            }
            ++i;
        }
    }
    if (device["setup"].empty()) {
        device.removeMember("setup");
    }
}

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

    Json::Value deviceTemplate(templates.GetTemplate(channel["device_type"].asString()));
    Json::Value filteredChannels = FilterStandardChannels(channel, deviceTemplate, templates, subdeviceTypeHashes);
    channel.removeMember("standard_channels");
    if ( channel.isMember("channels") ) {
        for (Json::Value& ch : filteredChannels) {
            channel["channels"].append(ch);
        }
    } else {
        channel["channels"] = filteredChannels;
    }
    if (channel["channels"].empty()) {
        channel.removeMember("channels");
    }

    TransformSetupParams(channel, deviceTemplate);

    return true;
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
            if (TryToTransformSubDeviceChannel(ch, templates, subdeviceTypeHashes)) {
                channels.append(ch);
            } else {
                bool hasPollInterval = false;
                if (ch.isMember("poll_interval")) {
                    hasPollInterval = true;
                    int pollIntervalToErase = DefaultPollInterval.count();
                    if (it->second.isMember("poll_interval")) {
                        pollIntervalToErase = it->second["poll_interval"].asInt();
                    }
                    if (ch["poll_interval"].asInt() == pollIntervalToErase) {
                        ch.removeMember("poll_interval");
                        hasPollInterval = false;
                    }
                }
                if ((ch.isMember("enabled") && !ch["enabled"].asBool()) || hasPollInterval) {
                    channels.append(ch);
                }
            }
        }
    }

    return channels;
}

Json::Value MakeConfigFromConfed(std::istream& stream, TTemplateMap& templates)
{
    Json::Value  root;
    Json::Reader reader;

    if (!reader.parse(stream, root, false)) {
        throw std::runtime_error("Failed to parse JSON:" + reader.getFormattedErrorMessages());
    }

    std::unordered_map<std::string, std::string> deviceTypeHashes;
    for (const auto& dt: templates.GetDeviceTypes()) {
        deviceTypeHashes[GetDeviceKey(dt)] = dt;
    }

    for (Json::Value& port : root["ports"]) {
        for (Json::Value& device : port["devices"]) {

            RemoveDeviceHash(device, deviceTypeHashes);
            auto dt = device["device_type"].asString();

            Json::Value deviceTemplate(templates.GetTemplate(dt));
            TSubDevicesTemplateMap subdevices(deviceTemplate);
            std::unordered_map<std::string, std::string> subdeviceTypeHashes;
            for (const auto& dt: subdevices.GetDeviceTypes()) {
                subdeviceTypeHashes[GetSubdeviceKey(dt)] = dt;
            }

            Json::Value filteredChannels = FilterStandardChannels(device, deviceTemplate, subdevices, subdeviceTypeHashes);
            device.removeMember("standard_channels");
            if ( device.isMember("channels") ) {
                for (Json::Value& ch : filteredChannels) {
                    device["channels"].append(ch);
                }
            } else {
                device["channels"] = filteredChannels;
            }
            if (device["channels"].empty()) {
                device.removeMember("channels");
            }

            TransformSetupParams(device, deviceTemplate);
            if (dt == CUSTOM_DEVICE_TYPE) {
                device.removeMember("device_type");
            }
        }
    }

    return root;
}
