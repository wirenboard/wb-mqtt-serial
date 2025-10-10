#include "old_serial_config.h"
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

    void FixChannel(Json::Value& channel)
    {
        ConvertPollIntervalToReadRateLimit(channel);
    }

    void FixChannels(Json::Value& device, TTemplateMap& templates)
    {
        // Some configs have settings for channels from old templates or manually added channels.
        // Result may contain only manually added channels and channels declared in device template.
        Json::Value channels;
        PDeviceTemplate deviceTemplate;
        if (device.isMember("device_type")) {
            auto deviceType = device["device_type"].asString();
            try {
                deviceTemplate = templates.GetTemplate(deviceType);
            } catch (const std::out_of_range&) {
                // Pass unknown device type as is
                return;
            }
        }
        for (Json::Value& deviceChannel: device["channels"]) {
            FixChannel(deviceChannel);
            if (deviceChannel.isMember(SerialConfig::ADDRESS_PROPERTY_NAME) ||
                deviceChannel.isMember(SerialConfig::WRITE_ADDRESS_PROPERTY_NAME) ||
                deviceChannel.isMember("consists_of"))
            {
                channels.append(deviceChannel);
                continue;
            }

            if (deviceTemplate) {
                for (const Json::Value& templateChannel: deviceTemplate->GetTemplate()["channels"]) {
                    if (deviceChannel["name"] == templateChannel["name"]) {
                        channels.append(deviceChannel);
                        break;
                    }
                }
            }
        }
        device.removeMember("channels");
        if (!channels.empty()) {
            device["channels"] = channels;
        }
    }

    void FixPassword(Json::Value& device)
    {
        if (device.isMember("password") && device["password"].isArray()) {
            for (Json::Value& byte: device["password"]) {
                if (!byte.isString()) {
                    byte = byte.asString();
                }
            }
        }
    }

    void FixDevice(Json::Value& device)
    {
        ConvertPollIntervalToReadRateLimit(device);
        FixPassword(device);
        if (device.isMember("slave_id")) {
            // Old configs could have slave_id defined as number not as string.
            // Convert numbers to strings.
            if (device["slave_id"].isNumeric()) {
                device["slave_id"] = device["slave_id"].asString();
            } else {
                // Broadcast address in old configs could be empty string. Remove it.
                if (device["slave_id"].isString() && device["slave_id"].asString().empty()) {
                    device.removeMember("slave_id");
                }
            }
        }
    }
}

void FixOldConfigFormat(Json::Value& config, TTemplateMap& templates)
{
    for (auto& port: config["ports"]) {
        ConvertPollIntervalToReadRateLimit(port);
        for (auto& device: port["devices"]) {
            FixDevice(device);
            FixChannels(device, templates);
        }
    }
}
