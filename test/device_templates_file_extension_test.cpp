#include <gtest/gtest.h>

#include <wblib/testing/testlog.h>
#include "serial_config.h"

using WBMQTT::Testing::TLoggedFixture;

class TDeviceTemplateFileExtensionTest: public ::testing::Test {
protected:
    void VerifyTemplates(const std::string& directory, const std::string& bad_device_type);
};

void TDeviceTemplateFileExtensionTest::VerifyTemplates(const std::string& directory, const std::string& bad_device_type)
{
    try {
        Json::Value configSchema = LoadConfigSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial.schema.json"));
        auto templates = LoadConfigTemplates(directory,
                                             LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), 
                                                                       configSchema));
        ASSERT_FALSE(templates.count(bad_device_type));
    } catch (const TConfigParserException& e) {
        ADD_FAILURE() << "Parsing failed: " << e.what();
    }
}

TEST_F(TDeviceTemplateFileExtensionTest, WrongExtension)
{
    VerifyTemplates(TLoggedFixture::GetDataFilePath("device-templates"), "MSU34_BAD");
}
