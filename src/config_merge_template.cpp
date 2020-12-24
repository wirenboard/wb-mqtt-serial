#include "config_merge_template.h"
#include "serial_config.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

void UpdateChannels(Json::Value& dst, const Json::Value& userConfig, ITemplateMap& channelTemplates, const std::string& logPrefix);

void AppendSetupItems(Json::Value& dst, const Json::Value& src)
{
    if (!dst.isMember("setup")) {
        dst["setup"] = Json::Value(Json::arrayValue);
    }

    Json::Value newDst(Json::arrayValue);

    std::unordered_map<std::string, Json::Value> templateSetup;
    for (auto& item: dst["setup"]) {
        templateSetup.insert( {item["title"].asString(), item} );
    }

    if (src.isMember("setup")) {
        for (auto& item: src["setup"]) {
            if (item.isMember("title")) {
                auto itTemplate = templateSetup.find(item["title"].asString());
                if (itTemplate != templateSetup.end()) {
                    Json::Value res;
                    res["title"] = item["title"];
                    res["address"] = itTemplate->second["address"];
                    res["value"] = item["value"];
                    newDst.append(res);
                    templateSetup.erase(itTemplate);
                    continue;
                }
            }
            if (!item.isMember("address")) {
                throw TConfigParserException("Setup command '" + item["title"].asString() + "' must have address");
            }
            newDst.append(item);
        }
    }

    if (dst.isMember("setup")) {
        for (auto& item: dst["setup"]) {
            auto itTemplate = templateSetup.find(item["title"].asString());
            if (itTemplate != templateSetup.end()) {
                if (item.isMember("value")) {
                    newDst.append(item);
                }
            }
        }
    }

    if (newDst.empty()) {
        dst.removeMember("setup");
    } else {
        dst["setup"] = newDst;
    }
}

void SetPropertyWithNotification(Json::Value& dst, Json::Value::const_iterator itProp, const std::string& logPrefix) {
    if (dst.isMember(itProp.name()) && dst[itProp.name()] != *itProp) {
        LOG(Info) << logPrefix << " override property \"" << itProp.name() << "\"";
    }
    dst[itProp.name()] = *itProp;
}

void CheckDeviceType(Json::Value& templateConfig, const Json::Value& userConfig, const std::string& logPrefix)
{
    if (templateConfig.isMember("oneOf")) {
        if (!userConfig.isMember("device_type")) {
            throw TConfigParserException("'" + logPrefix + "' must contain device_type");
        }
        for (const auto& item: templateConfig["oneOf"]) {
            if (item == userConfig["device_type"]) {
                templateConfig["device_type"] = item;
                return;
            }
        }
        throw TConfigParserException("'" + logPrefix + "' can't have type '" + userConfig["device_type"].asString() + "'");
    }
    if (templateConfig.isMember("device_type")) {
        if (!userConfig.isMember("device_type") || (userConfig["device_type"] != templateConfig["device_type"])) {
            throw TConfigParserException("'" + logPrefix + "' device_type must be '" + templateConfig["device_type"].asString() + "'");
        }
        return;
    }

    if (userConfig.isMember("device_type")) {
        throw TConfigParserException("'" + logPrefix + "' can't contain device_type");
    }
}

void MergeChannelProperties(Json::Value& templateConfig, const Json::Value& userConfig, const std::string& logPrefix)
{
    CheckDeviceType(templateConfig, userConfig, logPrefix);

    for (auto itProp = userConfig.begin(); itProp != userConfig.end(); ++itProp) {
        if (itProp.name() == "poll_interval" || itProp.name() == "enabled") {
            SetPropertyWithNotification(templateConfig, itProp, logPrefix);
            continue;
        }
        if (itProp.name() == "readonly") {
            if ((itProp ->asString() != "true") && (templateConfig.isMember(itProp.name())) && (templateConfig[itProp.name()].asString() == "true")) {
                LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
                continue;
            }
            SetPropertyWithNotification(templateConfig, itProp, logPrefix);
            continue;
        }

        if (itProp.name() == "name") {
            continue;
        }
        if (itProp.name() == "device_type") {
            continue;
        }

        if (itProp.name() == "channels") {
            templateConfig["channels"] = *itProp;
            continue;
        }
        if (itProp.name() == "setup") {
            templateConfig["setup"] = *itProp;
            continue;
        }

        LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
    }
}

Json::Value MergeChannelConfigWithTemplate(const Json::Value& channelConfig, ITemplateMap& templates, const std::string& logPrefix)
{
    if (!channelConfig.isMember("device_type")) {
        return channelConfig;
    }

    Json::Value res(templates.GetTemplate(channelConfig["device_type"].asString()));
    res["device_type"] = channelConfig["device_type"];
    SetIfExists(res, "name", channelConfig, "name");
    SetIfExists(res, "shift", channelConfig, "shift");
    SetIfExists(res, "stride", channelConfig, "stride");
    AppendSetupItems(res, channelConfig);
    UpdateChannels(res["channels"], channelConfig["channels"], templates, logPrefix + " " + channelConfig["name"].asString());
    return res;
}

void UpdateChannels(Json::Value& channelsFromTemplate, const Json::Value& userChannels, ITemplateMap& channelTemplates, const std::string& logPrefix)
{
    std::unordered_map<std::string, Json::ArrayIndex> channelNames;

    for (Json::ArrayIndex i = 0; i < channelsFromTemplate.size(); ++i) {
        channelNames.emplace(channelsFromTemplate[i]["name"].asString(), i);
    }

    for (const auto& elem: userChannels) {
        auto channelName(elem["name"].asString());
        if (channelNames.count(channelName)) {
            MergeChannelProperties(channelsFromTemplate[channelNames[channelName]], elem, logPrefix + " channel \"" + channelName + "\"");
        } else {
            channelsFromTemplate.append(elem);
        }
    }

    for (auto& elem: channelsFromTemplate) {
        elem = MergeChannelConfigWithTemplate(elem, channelTemplates, logPrefix);
    }
}

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData, ITemplateMap& templates)
{
    if (!deviceData.isMember("device_type")) {
        return deviceData;
    }

    Json::Value res(templates.GetTemplate(deviceData["device_type"].asString()));
    TSubDevicesTemplateMap subDevicesTemplates(res);
    res.removeMember("subdevices");

    std::string deviceName;
    if (deviceData.isMember("name")) {
        deviceName = deviceData["name"].asString();
    } else {
        deviceName = res["name"].asString() + DecorateIfNotEmpty(" ", deviceData["slave_id"].asString());
    }
    res["name"] = deviceName;

    if (res.isMember("id")) {
        res["id"] = res["id"].asString() + DecorateIfNotEmpty("_", deviceData["slave_id"].asString());
    }

    for (auto itProp = deviceData.begin(); itProp != deviceData.end(); ++itProp) {
        if (itProp.name() != "channels" && itProp.name() != "setup" && itProp.name() != "name") {
            SetPropertyWithNotification(res, itProp, deviceName);
        }
    }

    AppendSetupItems(res, deviceData);
    UpdateChannels(res["channels"], deviceData["channels"], subDevicesTemplates, "\"" + deviceName + "\"");

    return res;
}