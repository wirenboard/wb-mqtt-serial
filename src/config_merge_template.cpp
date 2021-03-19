#include "config_merge_template.h"
#include "serial_config.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

void UpdateChannels(Json::Value& dst, const Json::Value& userConfig, ITemplateMap& channelTemplates, const std::string& logPrefix);

void AppendSetupItems(Json::Value& deviceTemplate, const Json::Value& config)
{
    if (!deviceTemplate.isMember("setup")) {
        deviceTemplate["setup"] = Json::Value(Json::arrayValue);
    }

    auto& newSetup = deviceTemplate["setup"];

    if (config.isMember("setup")) {
        for (const auto& item: config["setup"]) {
            if (!item.isMember("address")) {
                throw TConfigParserException("Setup command '" + item["title"].asString() + "' must have address");
            }
            newSetup.append(item);
        }
    }

    if (deviceTemplate.isMember("parameters")) {
        for (auto it = deviceTemplate["parameters"].begin(); it != deviceTemplate["parameters"].end(); ++it) {
            if (config.isMember(it.name())) {
                auto& cfgItem = config[it.name()];
                if (cfgItem.asInt()) {
                    Json::Value item;
                    item["value"] = cfgItem;
                    item["address"] = (*it)["address"];
                    item["title"] = (*it)["title"];
                    newSetup.append(item);
                } else {
                    LOG(Warn) << it.name() << " is not an integer";
                }
            }
        }
    }

    if (newSetup.empty()) {
        deviceTemplate.removeMember("setup");
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

    const std::vector<std::string> simpleChannelforbiddenOverrides({
        "id",
        "type",
        "reg_type",
        "address",
        "format",
        "max",
        "scale",
        "offset",
        "round_to",
        "on_value",
        "error_value",
        "unsupported_value",
        "word_order",
        "consists_of"});

    const std::vector<std::string> subdeviceChannelforbiddenOverrides({
        "id",
        "oneOf"});

    const auto& forbiddenOverrides =   (templateConfig.isMember("device_type") || templateConfig.isMember("oneOf")) 
                                     ? subdeviceChannelforbiddenOverrides
                                     : simpleChannelforbiddenOverrides;

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

        if (std::find(forbiddenOverrides.begin(), forbiddenOverrides.end(), itProp.name()) != forbiddenOverrides.end()) {
            LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
            continue;
        }

        templateConfig[itProp.name()] = *itProp;
    }
}

Json::Value MergeChannelConfigWithTemplate(const Json::Value& channelConfig, ITemplateMap& templates, const std::string& logPrefix)
{
    if (!channelConfig.isMember("device_type")) {
        return channelConfig;
    }

    Json::Value res(templates.GetTemplate(channelConfig["device_type"].asString()).Schema);
    res["device_type"] = channelConfig["device_type"];
    SetIfExists(res, "name", channelConfig, "name");
    SetIfExists(res, "shift", channelConfig, "shift");
    SetIfExists(res, "stride", channelConfig, "stride");
    SetIfExists(res, "id", channelConfig, "id");
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
            if (elem.isMember("device_type")) {
                throw TConfigParserException(logPrefix + " channel \"" + channelName + "\" can't contain \"device_type\" property");
            }
            channelsFromTemplate.append(elem);
        }
    }

    for (auto& elem: channelsFromTemplate) {
        elem = MergeChannelConfigWithTemplate(elem, channelTemplates, logPrefix);
    }
}

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceData,
                                          const std::string& deviceType,
                                          const Json::Value& deviceTemplate)
{

    if (deviceTemplate.empty()) {
        return deviceData;
    }

    auto res(deviceTemplate);

    TSubDevicesTemplateMap subDevicesTemplates(deviceType, deviceTemplate);
    res.removeMember("subdevices");

    std::string deviceName;
    if (deviceData.isMember("name")) {
        deviceName = deviceData["name"].asString();
    } else {
        deviceName = deviceTemplate["name"].asString() + DecorateIfNotEmpty(" ", deviceData["slave_id"].asString());
    }
    res["name"] = deviceName;

    if (deviceTemplate.isMember("id")) {
        res["id"] = deviceTemplate["id"].asString() + DecorateIfNotEmpty("_", deviceData["slave_id"].asString());
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