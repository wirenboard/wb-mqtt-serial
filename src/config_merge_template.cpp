#include "config_merge_template.h"
#include "expression_evaluator.h"
#include "log.h"
#include "serial_config.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;
using Expressions::TExpressionsCache;

namespace
{
    void RemoveDisabledChannels(Json::Value& config, const Json::Value& deviceData, TExpressionsCache& exprs)
    {
        TJsonParams params(deviceData);
        std::vector<Json::ArrayIndex> channelsToRemove;
        auto& channels = config["channels"];
        for (Json::ArrayIndex i = 0; i < channels.size(); ++i) {
            if (!CheckCondition(channels[i], params, &exprs)) {
                channelsToRemove.emplace_back(i);
            }
        }
        for (auto it = channelsToRemove.rbegin(); it != channelsToRemove.rend(); ++it) {
            channels.removeIndex(*it, nullptr);
        }
    }
}

void UpdateChannels(Json::Value& dst,
                    const Json::Value& userConfig,
                    TSubDevicesTemplateMap& channelTemplates,
                    const std::string& logPrefix);

void AppendSetupItems(Json::Value& deviceTemplate, const Json::Value& config, TExpressionsCache* exprs = nullptr)
{
    Json::Value newSetup(Json::arrayValue);

    TJsonParams params(config);

    if (config.isMember("setup")) {
        for (const auto& item: config["setup"]) {
            if (!item.isMember("address")) {
                throw TConfigParserException("Setup command '" + item["title"].asString() + "' must have address");
            }
            newSetup.append(item);
        }
    }

    if (deviceTemplate.isMember("parameters")) {
        Json::Value& templateParameters = deviceTemplate["parameters"];
        for (auto it = templateParameters.begin(); it != templateParameters.end(); ++it) {
            auto name = templateParameters.isArray() ? (*it)["id"].asString() : it.name();
            if (config.isMember(name)) {
                auto& cfgItem = config[name];
                if (cfgItem.isNumeric()) {
                    // Readonly parameters are used now only for web-interface organization.
                    if (!it->get("readonly", false).asBool() && CheckCondition(*it, params, exprs)) {
                        Json::Value item(*it);
                        item["value"] = cfgItem;
                        if (!templateParameters.isArray()) {
                            item["id"] = name;
                        }
                        newSetup.append(item);
                    }
                } else {
                    LOG(Warn) << name << " is not an integer";
                }
                deviceTemplate.removeMember(name);
            }
        }
        deviceTemplate.removeMember("parameters");
    }

    if (deviceTemplate.isMember("setup")) {
        for (const auto& item: deviceTemplate["setup"]) {
            if (CheckCondition(item, params, exprs)) {
                newSetup.append(item);
            }
        }
    }

    if (newSetup.empty()) {
        deviceTemplate.removeMember("setup");
    } else {
        deviceTemplate["setup"] = newSetup;
    }
}

void SetPropertyWithNotification(Json::Value& dst, Json::Value::const_iterator itProp, const std::string& logPrefix)
{
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
        throw TConfigParserException("'" + logPrefix + "' can't have type '" + userConfig["device_type"].asString() +
                                     "'");
    }
    if (templateConfig.isMember("device_type")) {
        if (!userConfig.isMember("device_type") || (userConfig["device_type"] != templateConfig["device_type"])) {
            throw TConfigParserException("'" + logPrefix + "' device_type must be '" +
                                         templateConfig["device_type"].asString() + "'");
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

    // clang-format off
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
        "off_value",
        "error_value",
        "unsupported_value",
        "word_order",
        "byte_order",
        "consists_of"});

    const std::vector<std::string> subdeviceChannelforbiddenOverrides({
        "id",
        "oneOf"});
    // clang-format on

    const auto& forbiddenOverrides = (templateConfig.isMember("device_type") || templateConfig.isMember("oneOf"))
                                         ? subdeviceChannelforbiddenOverrides
                                         : simpleChannelforbiddenOverrides;

    for (auto itProp = userConfig.begin(); itProp != userConfig.end(); ++itProp) {
        if (itProp.name() == "enabled") {
            SetPropertyWithNotification(templateConfig, itProp, logPrefix);
            continue;
        }
        if (itProp.name() == "readonly") {
            if ((itProp->asString() != "true") && (templateConfig.isMember(itProp.name())) &&
                (templateConfig[itProp.name()].asString() == "true"))
            {
                LOG(Warn) << logPrefix << " \"readonly\" is already set to \"true\" in template";
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

        if (std::find(forbiddenOverrides.begin(), forbiddenOverrides.end(), itProp.name()) != forbiddenOverrides.end())
        {
            LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
            continue;
        }

        templateConfig[itProp.name()] = *itProp;
    }
}

Json::Value MergeChannelConfigWithTemplate(const Json::Value& channelConfig,
                                           TSubDevicesTemplateMap& templates,
                                           const std::string& logPrefix)
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
    UpdateChannels(res["channels"],
                   channelConfig["channels"],
                   templates,
                   logPrefix + " " + channelConfig["name"].asString());
    return res;
}

void UpdateChannels(Json::Value& channelsFromTemplate,
                    const Json::Value& userChannels,
                    TSubDevicesTemplateMap& channelTemplates,
                    const std::string& logPrefix)
{
    std::unordered_map<std::string, std::vector<Json::ArrayIndex>> channelNames;

    for (Json::ArrayIndex i = 0; i < channelsFromTemplate.size(); ++i) {
        channelNames[channelsFromTemplate[i]["name"].asString()].push_back(i);
    }

    for (const auto& elem: userChannels) {
        auto channelName(elem["name"].asString());
        if (channelNames.count(channelName)) {
            for (const auto& chIt: channelNames[channelName]) {
                MergeChannelProperties(channelsFromTemplate[chIt],
                                       elem,
                                       logPrefix + " channel \"" + channelName + "\"");
            }
        } else {
            if (elem.isMember("device_type")) {
                throw TConfigParserException(logPrefix + " channel \"" + channelName +
                                             "\" can't contain \"device_type\" property");
            }
            channelsFromTemplate.append(elem);
        }
    }

    for (auto& elem: channelsFromTemplate) {
        elem = MergeChannelConfigWithTemplate(elem, channelTemplates, logPrefix);
    }
}

Json::Value MergeDeviceConfigWithTemplate(const Json::Value& deviceConfigJson,
                                          const std::string& deviceType,
                                          const Json::Value& deviceTemplate)
{
    if (deviceTemplate.empty()) {
        return deviceConfigJson;
    }

    const auto& parameters = deviceTemplate["parameters"];
    auto deviceData(deviceConfigJson);
    auto res(deviceTemplate);

    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        const auto& item = *it;
        auto id = parameters.isObject() ? it.key().asString() : item["id"].asString();
        if (!deviceData.isMember(id) && item.isMember("default")) {
            deviceData[id] = item["default"];
        }
    }

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

    if (deviceData.isMember("protocol")) {
        LOG(Warn)
            << "\"" << deviceName
            << "\" has \"protocol\" property set in config. It is ignored. Protocol from device template will be used.";
    }

    const std::unordered_set<std::string> specialProperties({"channels", "setup", "name", "protocol"});
    for (auto itProp = deviceData.begin(); itProp != deviceData.end(); ++itProp) {
        if (!specialProperties.count(itProp.name())) {
            SetPropertyWithNotification(res, itProp, deviceName);
        }
    }

    TExpressionsCache expressionsCache;
    AppendSetupItems(res, deviceData, &expressionsCache);
    UpdateChannels(res["channels"], deviceData["channels"], subDevicesTemplates, "\"" + deviceName + "\"");
    RemoveDisabledChannels(res, deviceData, expressionsCache);

    return res;
}

TJsonParams::TJsonParams(const Json::Value& params): Params(params)
{}

std::optional<int32_t> TJsonParams::Get(const std::string& name) const
{
    const auto& param = Params[name];
    if (param.isInt()) {
        return std::optional<int32_t>(param.asInt());
    }
    return std::nullopt;
}

bool CheckCondition(const Json::Value& item, const TJsonParams& params, TExpressionsCache* exprs)
{
    if (!exprs) {
        return true;
    }
    auto cond = item["condition"].asString();
    if (cond.empty()) {
        return true;
    }
    try {
        auto itExpr = exprs->find(cond);
        if (itExpr == exprs->end()) {
            Expressions::TParser parser;
            itExpr = exprs->emplace(cond, parser.Parse(cond)).first;
        }
        return Expressions::Eval(itExpr->second.get(), params);
    } catch (const std::exception& e) {
        throw TConfigParserException("Error during expression \"" + cond + "\" evaluation: " + e.what());
    }
    return false;
}
