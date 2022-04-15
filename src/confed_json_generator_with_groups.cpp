#include "confed_json_generator_with_groups.h"
#include "confed_channel_modes.h"
#include "confed_schema_generator_with_groups.h"
#include "serial_config.h"

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

    std::pair<Json::Value, Json::Value> SplitChannels(const Json::Value& device, const Json::Value& deviceTemplate)
    {
        Json::Value channels;
        Json::Value customChannels(Json::arrayValue);
        std::unordered_map<std::string, Json::Value> regs;
        if (device.isMember("channels")) {
            for (const Json::Value& channel: device["channels"]) {
                regs[channel["name"].asString()] = channel;
            }
        }

        std::unordered_set<std::string> processedChannels;
        if (deviceTemplate.isMember("channels")) {
            size_t i = 0;
            for (const Json::Value& ch: deviceTemplate["channels"]) {
                auto it = regs.find(ch["name"].asString());
                if (it != regs.end()) { // template overriding
                    channels[GetChannelPropertyName(i)] = ConfigToHomeuiGroupChannel(it->second, i);
                    processedChannels.insert(it->first);
                }
                ++i;
            }
        }

        // Preserve custom channels order
        for (const Json::Value& ch: device["channels"]) {
            if (!processedChannels.count(ch["name"].asString())) {
                // Some configs have settings for channels from old templates.
                // They don't have appropriate definitions and will be treated as custom channels without addresses.
                // It could lead to json-editor confusing and wrong web UI generation
                // Just drop such channels
                // TODO: It is not a good solution and should be deleted after template versions implementation
                if (ch.isMember("address") || ch.isMember("consists_of")) {
                    customChannels.append(ch);
                }
            }
        }

        return std::make_pair(channels, customChannels);
    }
}

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
//      "name": ...,
//      "device_type": DT,
//      COMMON_DEVICE_SETUP_PARAMS,
//      "parameter1": PARAMETER1_VALUE,
//      ...
//      "setup": [ ... ],
//      "ch1": CHANNEL_VALUE, // standard channels converter to parameters
//      ...
//      "channels": [ ... ] // custom channels
//  }
Json::Value MakeDeviceWithGroupsForConfed(const Json::Value& config, const Json::Value& deviceTemplateSchema)
{
    auto dt = config["device_type"].asString();

    Json::Value newDev(config);
    ConvertPollIntervalToReadRateLimit(newDev);

    Json::Value customChannels;
    Json::Value standardChannels;
    std::tie(standardChannels, customChannels) = SplitChannels(newDev, deviceTemplateSchema);
    newDev.removeMember("channels");
    if (!customChannels.empty()) {
        newDev["channels"] = customChannels;
    }
    AppendParams(newDev, standardChannels);

    // Old configs could have slave_id defined as number not as string.
    // To not confuse users convert numbers to strings and show only string editor for slave_id.
    if (newDev.isMember("slave_id") && newDev["slave_id"].isNumeric()) {
        newDev["slave_id"] = newDev["slave_id"].asString();
    }
    if (!newDev.isMember("slave_id") || (newDev["slave_id"].isString() && newDev["slave_id"].asString().empty())) {
        newDev["slave_id"] = false;
    }
    return newDev;
}
