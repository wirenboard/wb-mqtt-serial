#include "templates_map.h"

#include <filesystem>

#include "expression_evaluator.h"
#include "file_utils.h"
#include "json_common.h"
#include "log.h"
#include "serial_config.h"

#define LOG(logger) ::logger.Log() << "[templates] "

using namespace std;
using namespace WBMQTT::JSON;

namespace
{
    bool EndsWith(const string& str, const string& suffix)
    {
        return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void CheckNesting(const Json::Value& root, size_t nestingLevel, TSubDevicesTemplateMap& templates)
    {
        if (nestingLevel > 5) {
            throw TConfigParserException(
                "Too deep subdevices nesting. This could be caused by cyclic subdevice dependencies");
        }
        for (const auto& ch: root["device"]["channels"]) {
            if (ch.isMember("device_type")) {
                CheckNesting(templates.GetTemplate(ch["device_type"].asString()).Schema, nestingLevel + 1, templates);
            }
            if (ch.isMember("oneOf")) {
                for (const auto& subdeviceType: ch["oneOf"]) {
                    CheckNesting(templates.GetTemplate(subdeviceType.asString()).Schema, nestingLevel + 1, templates);
                }
            }
        }
    }

    void ValidateCondition(const Json::Value& node, std::unordered_set<std::string>& validConditions)
    {
        if (node.isMember("condition")) {
            auto condition = node["condition"].asString();
            if (validConditions.find(condition) == validConditions.end()) {
                Expressions::TParser parser;
                parser.Parse(condition);
                validConditions.insert(condition);
            }
        }
    }

    std::string GetNodeName(const Json::Value& node, const std::string& name)
    {
        const std::vector<std::string> keys = {"name", "title"};
        for (const auto& key: keys) {
            if (node.isMember(key)) {
                return node[key].asString();
            }
        }
        return name;
    }

    void ValidateConditions(Json::Value& deviceTemplate)
    {
        std::unordered_set<std::string> validConditions;
        std::vector<std::string> sections = {"channels", "setup", "parameters"};
        for (const auto& section: sections) {
            if (deviceTemplate.isMember(section)) {
                Json::Value& sectionNodes = deviceTemplate[section];
                for (auto it = sectionNodes.begin(); it != sectionNodes.end(); ++it) {
                    try {
                        ValidateCondition(*it, validConditions);
                    } catch (const runtime_error& e) {
                        throw runtime_error("Failed to parse condition in " + section + "[" +
                                            GetNodeName(*it, it.name()) + "]: " + e.what());
                    }
                }
            }
        }
    }
}

//=============================================================================
//                                TTemplateMap
//=============================================================================
TTemplateMap::TTemplateMap(const Json::Value& templateSchema): Validator(new WBMQTT::JSON::TValidator(templateSchema))
{}

PDeviceTemplate TTemplateMap::MakeTemplateFromJson(const Json::Value& data, const std::string& filePath)
{
    std::string deviceType = data["device_type"].asString();
    auto deviceTemplate = std::make_shared<TDeviceTemplate>(deviceType,
                                                            data["device"].get("protocol", "modbus").asString(),
                                                            Validator,
                                                            filePath);
    deviceTemplate->SetTitle(GetTranslations(data.get("title", "").asString(), data["device"]));
    deviceTemplate->SetGroup(data.get("group", "").asString());
    if (data.get("deprecated", false).asBool()) {
        deviceTemplate->SetDeprecated();
    }
    if (data["device"].isMember("subdevices")) {
        deviceTemplate->SetWithSubdevices();
    }
    if (data.isMember("hw")) {
        std::vector<TDeviceTemplateHardware> hws;
        for (const auto& hwItem: data["hw"]) {
            TDeviceTemplateHardware hw;
            Get(hwItem, "signature", hw.Signature);
            Get(hwItem, "fw", hw.Fw);
            hws.push_back(std::move(hw));
        }
        deviceTemplate->SetHardware(hws);
    }
    deviceTemplate->SetMqttId(data["device"].get("id", "").asString());
    return deviceTemplate;
}

void TTemplateMap::AddTemplatesDir(const std::string& templatesDir,
                                   bool passInvalidTemplates,
                                   const Json::Value& settings)
{
    std::unique_lock m(Mutex);
    PreferredTemplatesDir = templatesDir;
    IterateDirByPattern(
        templatesDir,
        ".json",
        [&](const std::string& filepath) {
            if (!EndsWith(filepath, ".json")) {
                return false;
            }
            try {
                auto deviceTemplate =
                    MakeTemplateFromJson(WBMQTT::JSON::ParseWithSettings(filepath, settings), filepath);
                Templates.try_emplace(deviceTemplate->Type, std::vector<PDeviceTemplate>{})
                    .first->second.push_back(deviceTemplate);
            } catch (const std::exception& e) {
                if (passInvalidTemplates) {
                    LOG(Error) << "Failed to parse " << filepath << "\n" << e.what();
                    return false;
                }
                throw;
            }
            return false;
        },
        true);
}

PDeviceTemplate TTemplateMap::GetTemplate(const std::string& deviceType)
{
    try {
        std::unique_lock m(Mutex);
        return Templates.at(deviceType).back();
    } catch (const std::out_of_range&) {
        throw std::out_of_range("Can't find template for '" + deviceType + "'");
    }
}

std::vector<PDeviceTemplate> TTemplateMap::GetTemplates()
{
    std::unique_lock m(Mutex);
    std::vector<PDeviceTemplate> templates;
    for (const auto& t: Templates) {
        templates.push_back(t.second.back());
    }
    return templates;
}

std::vector<std::string> TTemplateMap::UpdateTemplate(const std::string& path)
{
    std::vector<std::string> res;
    if (!EndsWith(path, ".json")) {
        return res;
    }
    std::unique_lock m(Mutex);
    auto deletedType = DeleteTemplateUnsafe(path);
    if (!deletedType.empty()) {
        res.push_back(deletedType);
    }
    auto deviceTemplate = MakeTemplateFromJson(WBMQTT::JSON::Parse(path), path);
    auto& typeArray = Templates.try_emplace(deviceTemplate->Type, std::vector<PDeviceTemplate>{}).first->second;
    if (!PreferredTemplatesDir.empty() && WBMQTT::StringStartsWith(path, PreferredTemplatesDir)) {
        typeArray.push_back(deviceTemplate);
    } else {
        typeArray.insert(typeArray.begin(), deviceTemplate);
    }
    if (deviceTemplate->Type != deletedType) {
        res.push_back(deviceTemplate->Type);
    }
    return res;
}

std::string TTemplateMap::DeleteTemplateUnsafe(const std::string& path)
{
    for (auto& deviceTemplates: Templates) {
        auto item = std::find_if(deviceTemplates.second.begin(), deviceTemplates.second.end(), [&](const auto& t) {
            return t->GetFilePath() == path;
        });
        if (item != deviceTemplates.second.end()) {
            auto deviceType = deviceTemplates.first;
            if (deviceTemplates.second.size() > 1) {
                deviceTemplates.second.erase(item);
            } else {
                Templates.erase(deviceType);
            }
            return deviceType;
        }
    }

    return std::string();
}

std::string TTemplateMap::DeleteTemplate(const std::string& path)
{
    std::unique_lock m(Mutex);
    return DeleteTemplateUnsafe(path);
}

//=============================================================================
//                              TDeviceTemplate
//=============================================================================
TDeviceTemplate::TDeviceTemplate(const std::string& type,
                                 const std::string& protocol,
                                 std::shared_ptr<WBMQTT::JSON::TValidator> validator,
                                 const std::string& filePath)
    : Type(type),
      Deprecated(false),
      Validator(validator),
      FilePath(filePath),
      Subdevices(false),
      Protocol(protocol)
{}

std::string TDeviceTemplate::GetTitle(const std::string& lang) const
{
    auto it = Title.find(lang);
    if (it != Title.end()) {
        return it->second;
    }
    if (lang != "en") {
        it = Title.find("en");
        if (it != Title.end()) {
            return it->second;
        }
    }
    return Type;
}

const std::string& TDeviceTemplate::GetGroup() const
{
    return Group;
}

const std::vector<TDeviceTemplateHardware>& TDeviceTemplate::GetHardware() const
{
    return Hardware;
}

bool TDeviceTemplate::IsDeprecated() const
{
    return Deprecated;
}

void TDeviceTemplate::SetDeprecated()
{
    Deprecated = true;
}

void TDeviceTemplate::SetGroup(const std::string& group)
{
    if (!group.empty()) {
        Group = group;
    }
}

void TDeviceTemplate::SetTitle(const std::unordered_map<std::string, std::string>& translations)
{
    Title = translations;
}

void TDeviceTemplate::SetHardware(const std::vector<TDeviceTemplateHardware>& hardware)
{
    Hardware = hardware;
}

const std::string& TDeviceTemplate::GetFilePath() const
{
    return FilePath;
}

const Json::Value& TDeviceTemplate::GetTemplate()
{
    if (Template.isNull()) {
        Json::Value root(WBMQTT::JSON::Parse(GetFilePath()));
        // Skip deprecated template validation, it may be broken according to latest schema
        if (!IsDeprecated()) {
            try {
                Validator->Validate(root);
                ValidateConditions(root["device"]);
                // Check that parameters with same ids have same addresses (for parameters declared as array)
                ValidateParameterAddresses(root["device"]["parameters"]);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error("File: " + GetFilePath() + " error: " + e.what());
            }
            // Check that channels refer to valid subdevices and they are not nested too deep
            if (WithSubdevices()) {
                TSubDevicesTemplateMap subdevices(Type, root["device"]);
                CheckNesting(root, 0, subdevices);
            }
        }
        Template = root["device"];
    }
    return Template;
}

void TDeviceTemplate::SetWithSubdevices()
{
    Subdevices = true;
}

bool TDeviceTemplate::WithSubdevices() const
{
    return Subdevices;
}

const std::string& TDeviceTemplate::GetProtocol() const
{
    return Protocol;
}

void TDeviceTemplate::SetMqttId(const std::string& id)
{
    MqttId = id;
}

const std::string& TDeviceTemplate::GetMqttId() const
{
    return MqttId;
}

void TDeviceTemplate::ValidateParameterAddresses(const Json::Value& parameters)
{
    if (!parameters.isArray()) {
        return;
    }
    std::unordered_map<std::string, Json::Value> addressMap;
    std::unordered_map<std::string, Json::Value> writeAddressMap;
    std::string error;
    for (const auto& parameter: parameters) {
        std::string id = parameter["id"].asString();
        Json::Value address = parameter[SerialConfig::ADDRESS_PROPERTY_NAME];
        Json::Value writeAddress = parameter[SerialConfig::WRITE_ADDRESS_PROPERTY_NAME];
        auto addressIt = addressMap.find(id);
        if (addressIt != addressMap.end() && addressIt->second != address) {
            error = "Parameter \"" + id + "\" has several declarations with different \"" +
                    SerialConfig::ADDRESS_PROPERTY_NAME + "\" values (" + addressIt->second.asString() + " and " +
                    address.asString() + "). ";
            break;
        }
        addressMap[id] = address;
        auto writeAddressIt = writeAddressMap.find(id);
        if (writeAddressIt != writeAddressMap.end() && writeAddressIt->second != writeAddress) {
            error = "Parameter \"" + id + "\" has several declarations with different \"" +
                    SerialConfig::WRITE_ADDRESS_PROPERTY_NAME + "\" values (" + writeAddressIt->second.asString() +
                    " and " + writeAddress.asString() + "). ";
            break;
        }
        writeAddressMap[id] = writeAddress;
    }
    if (!error.empty()) {
        throw std::runtime_error(error + "All parameter declarations with the same id must have the same addresses.");
    }
}

//=============================================================================
//                          TSubDevicesTemplateMap
//=============================================================================
TSubDevicesTemplateMap::TSubDevicesTemplateMap(const std::string& deviceType, const Json::Value& device)
    : DeviceType(deviceType)
{
    if (device.isMember("subdevices")) {
        AddSubdevices(device["subdevices"]);

        // Check that channels refer to valid subdevices
        for (const auto& subdeviceTemplate: Templates) {
            for (const auto& ch: subdeviceTemplate.second.Schema["channels"]) {
                if (ch.isMember("device_type")) {
                    TSubDevicesTemplateMap::GetTemplate(ch["device_type"].asString());
                }
                if (ch.isMember("oneOf")) {
                    for (const auto& subdeviceType: ch["oneOf"]) {
                        TSubDevicesTemplateMap::GetTemplate(subdeviceType.asString());
                    }
                }
            }
        }
    }
}

void TSubDevicesTemplateMap::AddSubdevices(const Json::Value& subdevicesArray)
{
    for (auto& dev: subdevicesArray) {
        auto deviceType = dev["device_type"].asString();
        if (Templates.count(deviceType)) {
            LOG(Warn) << "Device type '" << DeviceType << "'. Duplicate subdevice type '" << deviceType << "'";
        } else {
            auto deviceTypeTitle = deviceType;
            Get(dev, "title", deviceTypeTitle);
            Templates.insert({deviceType, {deviceType, deviceTypeTitle, dev["device"]}});
        }
    }
}

const TSubDeviceTemplate& TSubDevicesTemplateMap::GetTemplate(const std::string& deviceType)
{
    try {
        return Templates.at(deviceType);
    } catch (const std::out_of_range&) {
        throw std::out_of_range("Device type '" + DeviceType + "'. Can't find template for subdevice '" + deviceType +
                                "'");
    }
}

std::vector<std::string> TSubDevicesTemplateMap::GetDeviceTypes() const
{
    std::vector<std::string> res;
    for (const auto& elem: Templates) {
        res.push_back(elem.first);
    }
    return res;
}
