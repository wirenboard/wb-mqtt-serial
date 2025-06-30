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

    Json::Value ConfigToHomeuiChannel(const Json::Value& channel)
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
        v["mode"] = IN_QUEUE_ORDER;
        return v;
    }
}

Json::Value Subdevices::HomeuiToConfigChannel(const Json::Value& ch)
{
    Json::Value res;
    if (ch.isMember("name")) {
        res["name"] = ch["name"];
    }
    if (ch.isMember("mode")) {
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
    }
    return res;
}

Json::Value Subdevices::ConfigToHomeuiSubdeviceChannel(const Json::Value& channel)
{
    Json::Value res(ConfigToHomeuiChannel(channel));
    res["name"] = channel["name"];
    return res;
}
