#include "templates_map.h"

#include <filesystem>

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
}

TTemplateMap::TTemplateMap(const std::string& templatesDir,
                           const Json::Value& templateSchema,
                           bool passInvalidTemplates)
    : Validator(new WBMQTT::JSON::TValidator(templateSchema))
{
    AddTemplatesDir(templatesDir, passInvalidTemplates);
}

PDeviceTemplate TTemplateMap::MakeTemplateFromJson(const Json::Value& data, const std::string& filePath)
{
    std::string deviceType = data["device_type"].asString();
    auto deviceTemplate = std::make_shared<TDeviceTemplate>(deviceType, Validator, filePath);
    deviceTemplate->SetTitle(GetTranslations(data.get("title", "").asString(), data["device"]));
    deviceTemplate->SetGroup(data.get("group", "").asString());
    if (data.get("deprecated", false).asBool()) {
        deviceTemplate->SetDeprecated();
    }
    if (data["device"].isMember("subdevices")) {
        deviceTemplate->SetWithSubdevices();
    }
    // if (data.isMember("hw")) {
    //     for (const auto& hwItem: data["hw"]) {
    //         TDeviceTemplateHardware hw;
    //         Get(hwItem, "signature", hw.Signature);
    //         Get(hwItem, "fw", hw.Fw);
    //         deviceTemplate->Hardware.push_back(std::move(hw));
    //     }
    // }
    return deviceTemplate;
}

void TTemplateMap::AddTemplatesDir(const std::string& templatesDir,
                                   bool passInvalidTemplates,
                                   const Json::Value& settings)
{
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
                Templates[deviceTemplate->Type] = deviceTemplate;
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

TDeviceTemplate& TTemplateMap::GetTemplate(const std::string& deviceType)
{
    try {
        return *Templates.at(deviceType);
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Can't find template for '" + deviceType + "'");
    }
}

std::vector<PDeviceTemplate> TTemplateMap::GetTemplates()
{
    std::vector<PDeviceTemplate> templates;
    for (const auto& t: Templates) {
        templates.push_back(t.second);
    }
    return templates;
}

TDeviceTemplate::TDeviceTemplate(const std::string& type,
                                 std::shared_ptr<WBMQTT::JSON::TValidator> validator,
                                 const std::string& filePath)
    : Type(type),
      Deprecated(false),
      Validator(validator),
      FilePath(filePath),
      Subdevices(false)
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
    } catch (...) {
        throw std::runtime_error("Device type '" + DeviceType + "'. Can't find template for subdevice '" + deviceType +
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
