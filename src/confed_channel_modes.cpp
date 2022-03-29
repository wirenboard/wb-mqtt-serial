#include "confed_channel_modes.h"
#include "json_common.h"

namespace
{
    const std::string ChannelModes[] =
        {"do not read", "in queue order", "200 ms", "100 ms", "custom period", "not faster than (deprecated)"};

    enum TModes
    {
        DO_NOT_READ = 0,
        IN_QUEUE_ORDER,
        FAST_200,
        FAST_100,
        FAST,
        READ_LIMIT
    };
}

Json::Value HomeuiToConfigChannel(const Json::Value& ch)
{
    Json::Value res;
    if (ch.isMember("name")) {
        res["name"] = ch["name"];
    }
    switch (ch["mode"].asInt()) {
        case IN_QUEUE_ORDER: {
            res["enabled"] = true;
            break;
        }
        case FAST_200: {
            res["enabled"] = true;
            res["read_period_ms"] = 200;
            break;
        }
        case FAST_100: {
            res["enabled"] = true;
            res["read_period_ms"] = 100;
            break;
        }
        case FAST: {
            res["enabled"] = true;
            res["read_period_ms"] = ch["period"];
            break;
        }
        case DO_NOT_READ: {
            res["enabled"] = false;
            break;
        }
        case READ_LIMIT: {
            res["enabled"] = true;
            res["read_rate_limit_ms"] = ch["period"];
        }
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
    channelSchema["properties"]["mode"]["default"] = IN_QUEUE_ORDER;
    // Do not allow to select "read limit" option. It could be shown only if read_rate_limit_ms is defined
    channelSchema["properties"]["mode"]["options"]["enum_hidden"].append(READ_LIMIT);
    auto& deps = MakeArray("mode", channelSchema["properties"]["period"]["options"]["dependencies"]);
    // "fast" enables read_period_ms editor
    deps.append(FAST);
    // "read limit" enables read_rate_limit_ms editor
    deps.append(READ_LIMIT);
}

Json::Value ConfigToHomeuiChannel(const Json::Value& channel, bool addDefaultMode)
{
    Json::Value v;
    if (channel.isMember("enabled") && !channel["enabled"].asBool()) {
        v["mode"] = DO_NOT_READ;
        return v;
    }
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
                v["period"] = channel["read_period_ms"];
                break;
        }
        return v;
    }
    if (channel.isMember("read_rate_limit_ms")) {
        v["mode"] = READ_LIMIT;
        v["period"] = channel["read_rate_limit_ms"];
        return v;
    }
    if (channel.isMember("poll_interval")) {
        v["mode"] = READ_LIMIT;
        v["period"] = channel["poll_interval"];
        return v;
    }
    if (addDefaultMode) {
        v["mode"] = IN_QUEUE_ORDER;
    }
    return v;
}
