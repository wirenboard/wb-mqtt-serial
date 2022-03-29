#include "confed_config_generator2.h"
#include "confed_channel_modes.h"
#include "confed_json_generator.h"
#include "confed_schema_generator.h"

namespace
{
    Json::Value TransformChannel(const Json::Value& ch, const Json::Value& channelTemplate)
    {
        auto res = HomeuiToConfigChannel(ch);
        if (res.isMember("read_rate_limit_ms") && channelTemplate.isMember("read_rate_limit_ms") &&
            (res["read_rate_limit_ms"].asInt() == channelTemplate["read_rate_limit_ms"].asInt()))
        {
            res.removeMember("read_rate_limit_ms");
        }
        if (res.isMember("enabled")) {
            bool enabledInTemplate = true;
            WBMQTT::JSON::Get(channelTemplate, "enabled", enabledInTemplate);
            if (res["enabled"].asBool() == enabledInTemplate) {
                res.removeMember("enabled");
            }
        }
        return res;
    }

    long GetChannelIndex(const std::string& name)
    {
        if (WBMQTT::StringStartsWith(name, "ch")) {
            char* end;
            long res = strtol(name.c_str() + 2, &end, 10);
            if (end == name.c_str() + name.size()) {
                return res;
            }
        }
        return -1;
    }
}

void MakeDeviceConfigFromConfed2(Json::Value& device, const Json::Value& deviceTemplate)
{
    Json::Value channels(Json::arrayValue);
    if (deviceTemplate.isMember("channels")) {
        std::vector<std::string> paramsToRemove;
        for (Json::ValueConstIterator it = device.begin(); it != device.end(); ++it) {
            auto channelIndex = GetChannelIndex(it.name());
            if (channelIndex >= 0) {
                paramsToRemove.push_back(it.name());
                if (deviceTemplate["channels"].size() > static_cast<Json::ArrayIndex>(channelIndex)) {
                    const auto& channelTemplate =
                        deviceTemplate["channels"][static_cast<Json::ArrayIndex>(channelIndex)];
                    auto channel = TransformChannel(*it, channelTemplate);
                    if (!channel.empty()) {
                        channel["name"] = channelTemplate["name"];
                        channels.append(channel);
                    }
                }
            }
        }
        for (const auto& param: paramsToRemove) {
            device.removeMember(param);
        }
    }
    for (Json::Value& ch: device["channels"]) {
        channels.append(ch);
    }
    device["channels"].swap(channels);
    if (device["channels"].empty()) {
        device.removeMember("channels");
    }
}
