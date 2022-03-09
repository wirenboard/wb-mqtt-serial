#include "confed_channel_modes.h"

namespace
{
    const std::string ChannelModes[] = {"in queue order", "fast (200ms)", "fast (100ms)", "fast", "do not read"};

    enum TModes
    {
        IN_QUEUE_ORDER = 0,
        FAST_200,
        FAST_100,
        FAST,
        DO_NOT_READ
    };
}

Json::Value HomeuiToConfigChannel(const Json::Value& ch)
{
    Json::Value res;
    res["name"] = ch["name"];
    switch (ch["mode"].asInt()) {
        case IN_QUEUE_ORDER:
            res["enabled"] = true;
            break;
        case FAST_200:
            res["enabled"] = true;
            res["read_period_ms"] = 200;
            break;
        case FAST_100:
            res["enabled"] = true;
            res["read_period_ms"] = 100;
            break;
        case FAST:
            res["enabled"] = true;
            res["read_period_ms"] = ch["read_period_ms"];
            break;
        case DO_NOT_READ:
            res["enabled"] = false;
            break;
    }
    return res;
}

void AddChannelModes(Json::Value& channelSchema)
{
    size_t i = 0;
    for (const auto& mode: ChannelModes) {
        channelSchema["properties"]["mode"]["enum"].append(i);
        ++i;
        channelSchema["properties"]["mode"]["options"]["enum_titles"].append(mode);
    }
    // "fast" enables read_period_ms editor
    channelSchema["properties"]["read_period_ms"]["options"]["dependencies"]["mode"] = 3;
}

Json::Value ConfigToHomeuiChannel(const Json::Value& channel)
{
    Json::Value v;
    v["name"] = channel["name"];
    if (channel.isMember("enabled") && !channel["enabled"].asBool()) {
        v["mode"] = DO_NOT_READ;
    } else {
        if (channel.isMember("read_period_ms")) {
            switch (channel["read_period_ms"].asInt()) {
                case 100:
                    v["mode"] = FAST_100;
                    break;
                case 200:
                    v["mode"] = FAST_200;
                    break;
                default:
                    v["mode"] = FAST;
                    v["read_period_ms"] = channel["read_period_ms"];
                    break;
            }
        } else {
            v["mode"] = IN_QUEUE_ORDER;
        }
    }
    return v;
}
