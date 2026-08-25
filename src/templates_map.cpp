#include "templates_map.h"

#include <filesystem>
#include <unordered_set>

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
    //! Template sections whose items may have a "condition"
    const std::vector<std::string> CONDITION_SECTIONS = {"channels", "setup", "parameters"};

    //! Must be equal in all declarations of a parameter
    const std::vector<std::string> COMMON_PARAMETER_PROPERTIES = {SerialConfig::WRITE_ADDRESS_PROPERTY_NAME,
                                                                  SerialConfig::ADDRESS_PROPERTY_NAME,
                                                                  SerialConfig::FW_VERSION_PROPERTY_NAME};

    //! Define how a register is read and converted, must be equal in all declarations
    //! of a parameter used in conditions. Kept in sync with LoadRegisterConfig
    const std::vector<std::string> REGISTER_READING_PROPERTIES =
        {"reg_type", "format", "scale", "offset", "round_to", "word_order", "byte_order"};

    bool EndsWith(const string& str, const string& suffix)
    {
        return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void FixChannelsEnum(Json::Value& node)
    {
        if (node.isObject()) {
            if (node.isMember("channels") && node["channels"].isArray()) {
                for (Json::Value& channel: node["channels"]) {
                    FixChannelEnum(channel);
                }
            }
            for (const auto& member: node.getMemberNames()) {
                FixChannelsEnum(node[member]);
            }
        } else if (node.isArray()) {
            for (Json::Value& item: node) {
                FixChannelsEnum(item);
            }
        }
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

    void ValidateConditionAndAddDependencies(Json::Value& node, Expressions::TExpressionsCache& exprCache)
    {
        if (node.isMember("condition")) {
            const auto dependencies = Expressions::GetDependencies(node["condition"].asString(), exprCache);
            if (!dependencies.empty()) {
                Json::Value dependenciesArray(Json::arrayValue);
                for (const auto& dep: dependencies) {
                    dependenciesArray.append(dep);
                }
                node["dependencies"] = dependenciesArray;
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

    //! True for protocols that reject inconsistent bit offset/width between "write_address" and "address".
    bool ProtocolRestrictsWriteAddress(const std::string& protocol)
    {
        return protocol == "modbus" || protocol == "modbus-tcp" || protocol == "modbus_io" ||
               protocol == "modbus_io-tcp";
    }

    void ValidateRegisterAddressesInTemplate(const Json::Value& regCfg, bool restrictWriteAddress)
    {
        if (!restrictWriteAddress) {
            return;
        }
        if (!HasNoEmptyProperty(regCfg, SerialConfig::WRITE_ADDRESS_PROPERTY_NAME)) {
            return;
        }
        auto writeAddress = LoadRegisterBitsAddress(regCfg, SerialConfig::WRITE_ADDRESS_PROPERTY_NAME);
        if (writeAddress.BitWidth == 0) {
            return;
        }
        if (!HasNoEmptyProperty(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME)) {
            throw TConfigParserException(
                "\"write_address\" with bit offset/width requires \"address\" to read the other bits");
        }
        auto addr = LoadRegisterBitsAddress(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME);
        if (writeAddress.BitOffset != addr.BitOffset || writeAddress.BitWidth != addr.BitWidth) {
            throw TConfigParserException("Bit offset/width in \"write_address\" must match \"address\"");
        }
    }

    void ValidateChannelAddresses(const Json::Value& channel, bool restrictWriteAddress)
    {
        if (channel.isMember("consists_of")) {
            for (const auto& part: channel["consists_of"]) {
                ValidateRegisterAddressesInTemplate(part, restrictWriteAddress);
            }
        } else {
            ValidateRegisterAddressesInTemplate(channel, restrictWriteAddress);
        }
    }

    void ValidateTemplateSections(const Json::Value& deviceTemplate, const std::string& protocol)
    {
        Expressions::TExpressionsCache exprCache;
        const bool restrictWriteAddress = ProtocolRestrictsWriteAddress(protocol);
        for (const auto& section: CONDITION_SECTIONS) {
            if (deviceTemplate.isMember(section)) {
                const Json::Value& sectionNodes = deviceTemplate[section];
                for (auto it = sectionNodes.begin(); it != sectionNodes.end(); ++it) {
                    try {
                        if (it->isMember("condition")) {
                            auto condition = (*it)["condition"].asString();
                            if (exprCache.find(condition) == exprCache.end()) {
                                Expressions::TParser parser;
                                exprCache.emplace(condition, parser.Parse(condition));
                            }
                        }
                    } catch (const runtime_error& e) {
                        throw runtime_error("Failed to parse condition in " + section + "[" +
                                            GetNodeName(*it, it.name()) + "]: " + e.what());
                    }
                    if (section == "channels") {
                        try {
                            ValidateChannelAddresses(*it, restrictWriteAddress);
                        } catch (const runtime_error& e) {
                            throw runtime_error("Failed to validate addresses in " + section + "[" +
                                                GetNodeName(*it, it.name()) + "]: " + e.what());
                        }
                    }
                }
            }
        }
    }

    //! Collects ids of the parameters referenced in conditions.
    //! Conditions are already validated by ValidateTemplateSections, so parse errors are not decorated here
    std::unordered_set<std::string> CollectConditionParameterIds(const Json::Value& deviceTemplate)
    {
        std::unordered_set<std::string> res;
        Expressions::TExpressionsCache exprCache;
        for (const auto& section: CONDITION_SECTIONS) {
            for (const auto& node: deviceTemplate[section]) {
                auto dependencies = Expressions::GetDependencies(node["condition"].asString(), exprCache);
                res.insert(dependencies.begin(), dependencies.end());
            }
        }
        return res;
    }

    void AddDependenciesToTemplateSections(Json::Value& deviceTemplate)
    {
        Expressions::TExpressionsCache exprCache;
        for (const auto& section: CONDITION_SECTIONS) {
            if (deviceTemplate.isMember(section)) {
                Json::Value& sectionNodes = deviceTemplate[section];
                for (auto it = sectionNodes.begin(); it != sectionNodes.end(); ++it) {
                    ValidateConditionAndAddDependencies(*it, exprCache);
                }
            }
        }
    }

    void ValidateParameterProperties(const Json::Value& deviceTemplate)
    {
        const Json::Value& parameters = deviceTemplate["parameters"];
        if (!parameters.isArray()) {
            return;
        }
        auto conditionParameters = CollectConditionParameterIds(deviceTemplate);
        std::unordered_map<std::string, const Json::Value*> firstDeclarations;
        for (const auto& parameter: parameters) {
            auto id = parameter["id"].asString();
            auto insertRes = firstDeclarations.emplace(id, &parameter);
            if (insertRes.second) {
                continue;
            }
            const auto& firstDeclaration = *insertRes.first->second;
            auto propertyNames = COMMON_PARAMETER_PROPERTIES;
            if (conditionParameters.count(id)) {
                propertyNames.insert(propertyNames.end(),
                                     REGISTER_READING_PROPERTIES.begin(),
                                     REGISTER_READING_PROPERTIES.end());
            }
            for (const auto& propertyName: propertyNames) {
                auto firstValue = firstDeclaration[propertyName].asString();
                auto value = parameter[propertyName].asString();
                if (firstValue != value) {
                    throw std::runtime_error("Parameter \"" + id + "\" has several declarations with different \"" +
                                             propertyName + "\" values (\"" + firstValue + "\" and \"" + value +
                                             "\").");
                }
            }
        }
    }

    void TemplateUpdatedWarning(PDeviceTemplate deviceTemplate, const std::string& path)
    {
        LOG(Warn) << "Existing template data for device type '" << deviceTemplate->Type << "' (from file "
                  << deviceTemplate->GetFilePath() << ") replaced with contents of file " << path;
    }

    void ConvertParametersObjectToArray(Json::Value& deviceTemplate)
    {
        auto& device = deviceTemplate["device"];
        if (device.isMember("parameters") && device["parameters"].isObject()) {
            Json::Value parametersArray(Json::arrayValue);
            for (auto it = device["parameters"].begin(); it != device["parameters"].end(); ++it) {
                (*it)["id"] = it.key().asString();
                parametersArray.append((*it));
            }
            device["parameters"] = std::move(parametersArray);
        }
    }

    //! Throws std::runtime_error if the template is invalid. Doesn't modify the template
    void ValidateDeviceTemplate(const Json::Value& root, WBMQTT::JSON::TValidator& validator)
    {
        validator.Validate(root);
        ValidateTemplateSections(root["device"], root["device"].get("protocol", "modbus").asString());
        // Check declarations with the same id (for parameters declared as array)
        ValidateParameterProperties(root["device"]);
        // Check that channels refer to valid subdevices and they are not nested too deep
        if (root["device"].isMember("subdevices")) {
            TSubDevicesTemplateMap subdevices(root["device_type"].asString(), root["device"]);
            CheckNesting(root, 0, subdevices);
        }
    }

    //! Converts numeric channel enum values to strings
    void FixDeviceTemplate(Json::Value& root)
    {
        FixChannelsEnum(root);
    }

    void FixAndValidateDeviceTemplate(Json::Value& root, WBMQTT::JSON::TValidator& validator)
    {
        FixDeviceTemplate(root);
        ValidateDeviceTemplate(root, validator);
    }

    //! Prepares a valid template for use: adds condition dependencies, normalizes parameters
    void AnnotateDeviceTemplate(Json::Value& root)
    {
        AddDependenciesToTemplateSections(root["device"]);
        if (!root["device"].isMember("subdevices")) {
            ConvertParametersObjectToArray(root);
        }
    }
}

//=============================================================================
//                                TTemplateMap
//=============================================================================
TTemplateMap::TTemplateMap(const Json::Value& templateSchema, const std::string& userTemplatesDir)
    : Validator(new WBMQTT::JSON::TValidator(templateSchema)),
      UserTemplatesDir(userTemplatesDir)
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
    if (!UserTemplatesDir.empty() && WBMQTT::StringStartsWith(filePath, UserTemplatesDir)) {
        deviceTemplate->SetUserDefined();
    }
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
                auto typeData = Templates.try_emplace(deviceTemplate->Type, std::vector<PDeviceTemplate>{});
                if (!typeData.second) {
                    TemplateUpdatedWarning(typeData.first->second.back(), filepath);
                }
                typeData.first->second.push_back(deviceTemplate);
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
        if (!typeArray.empty()) {
            TemplateUpdatedWarning(typeArray.back(), path);
        }
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

void TTemplateMap::ValidateTemplate(const Json::Value& templateRoot)
{
    std::unique_lock m(Mutex);
    if (!Validator) {
        throw std::runtime_error("Device templates schema is not loaded");
    }
    Json::Value root(templateRoot);
    FixAndValidateDeviceTemplate(root, *Validator);
}

PDeviceTemplate TTemplateMap::FindUserDefinedTemplate(const std::string& deviceType)
{
    std::unique_lock m(Mutex);
    auto it = Templates.find(deviceType);
    if (it == Templates.end()) {
        return nullptr;
    }
    auto item = std::find_if(it->second.rbegin(), it->second.rend(), [](const auto& t) { return t->IsUserDefined(); });
    return item != it->second.rend() ? *item : nullptr;
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
      UserDefined(false),
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

bool TDeviceTemplate::IsUserDefined() const
{
    return UserDefined;
}

void TDeviceTemplate::SetUserDefined()
{
    UserDefined = true;
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
                FixAndValidateDeviceTemplate(root, *Validator);
            } catch (const std::runtime_error& e) {
                throw std::runtime_error("File: " + GetFilePath() + " error: " + e.what());
            }
            AnnotateDeviceTemplate(root);
        } else {
            FixDeviceTemplate(root);
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
