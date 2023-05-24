#include <dirent.h>
#include <gtest/gtest.h>

#include "serial_config.h"
#include <wblib/testing/testlog.h>

using WBMQTT::Testing::TLoggedFixture;

class TDeviceTemplateFileExtensionTest: public ::testing::Test
{
protected:
    void VerifyTemplates(const std::string& directory, const std::string& bad_device_type);
};

void TDeviceTemplateFileExtensionTest::VerifyTemplates(const std::string& directory, const std::string& bad_device_type)
{
    try {
        Json::Value configSchema = LoadConfigSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial.schema.json"));
        TTemplateMap templates(
            directory,
            LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                      configSchema));
        ASSERT_THROW(templates.GetTemplate(bad_device_type), std::runtime_error);
    } catch (const TConfigParserException& e) {
        ADD_FAILURE() << "Parsing failed: " << e.what();
    }
}

TEST_F(TDeviceTemplateFileExtensionTest, WrongExtension)
{
    VerifyTemplates(TLoggedFixture::GetDataFilePath("device-templates"), "MSU34_BAD");
}

class TDeviceTemplatesTest: public WBMQTT::Testing::TLoggedFixture
{
protected:
    void PrintDevice(const Json::Value& deviceTemplate,
                     const std::string& mqttPrefix,
                     ITemplateMap& templates,
                     size_t level)
    {
        std::map<std::string, Json::Value> channels; // Sort channels for stable test results
        for (const auto& subChannel: deviceTemplate["channels"]) {
            channels.emplace(subChannel["name"].asString(), subChannel);
        }
        for (const auto& subChannel: channels) {
            PrintChannel(subChannel.second, mqttPrefix, templates, level);
        }
    }

    void PrintChannel(const Json::Value& channel, const std::string& mqttPrefix, ITemplateMap& templates, size_t level)
    {
        auto name = GetName(channel, level);
        auto newMqttPrefix = GetNewMqttPrefix(channel, mqttPrefix);
        if (channel.isMember("oneOf")) {
            for (const auto& oneOfChannel: channel["oneOf"]) {
                auto subdeviceType = oneOfChannel.asString();
                Emit() << name << ": " << subdeviceType;
                PrintDevice(templates.GetTemplate(subdeviceType).Schema, newMqttPrefix, templates, level + 1);
            }
            return;
        }
        if (channel.isMember("device_type")) {
            auto subdeviceType = channel["device_type"].asString();
            Emit() << name << ": " << subdeviceType;
            PrintDevice(templates.GetTemplate(subdeviceType).Schema, newMqttPrefix, templates, level + 1);
            return;
        }
        Emit() << name << "  =>  " << newMqttPrefix;
    }

    std::string GetName(const Json::Value& channel, size_t level) const
    {
        std::string res;
        for (size_t i = 0; i < level; ++i) {
            res += '\t';
        }
        return res + channel["name"].asString();
    }

    std::string GetNewMqttPrefix(const Json::Value& channel, const std::string& prefix) const
    {
        auto id = channel.get("id", channel["name"]).asString();
        if (id.empty()) {
            return prefix;
        }
        if (prefix.empty()) {
            return id;
        }
        return prefix + " " + id;
    }
};

TEST_F(TDeviceTemplatesTest, Validate)
{
    SetMode(E_Normal);
    Json::Value configSchema(LoadConfigSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial.schema.json")));
    Json::Value templatesSchema(
        LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                  configSchema));
    TTemplateMap templates(TLoggedFixture::GetDataFilePath("../templates"), templatesSchema, false);
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("../build/templates"), false);
    auto deviceTypes = templates.GetDeviceTypes();
    std::sort(deviceTypes.begin(), deviceTypes.end()); // For stable test results
    for (const auto& deviceType: deviceTypes) {
        auto& dt = templates.GetTemplate(deviceType);
        TSubDevicesTemplateMap subdeviceTemplates(dt.Type, dt.Schema);
        Emit() << dt.Type;
        PrintDevice(dt.Schema, "", subdeviceTemplates, 1);
    }
}

TEST_F(TDeviceTemplatesTest, InvalidParameterName)
{
    Json::Value configSchema(LoadConfigSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial.schema.json")));
    Json::Value templatesSchema(
        LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                  configSchema));
    TTemplateMap templates(TLoggedFixture::GetDataFilePath("device-templates"), templatesSchema, false);
    EXPECT_THROW(templates.GetTemplate("parameters_array_invalid_id"), std::runtime_error);
    EXPECT_NO_THROW(templates.GetTemplate("parameters_object_invalid_name"));
    EXPECT_THROW(templates.GetTemplate("tpl1_parameters_object_invalid_name"), std::runtime_error);
}
