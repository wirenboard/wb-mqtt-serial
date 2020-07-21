#include <gtest/gtest.h>
#include <dirent.h>

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
        TTemplateMap templates(directory,
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

TEST(TDeviceTemplatesTest, Validate)
{
    DIR *dir;
    struct dirent *dirp;
    struct stat filestat;

    Json::Value configSchema(LoadConfigSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial.schema.json")));
    Json::Value templatesSchema(LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), configSchema));

    WBMQTT::JSON::TValidator validator(templatesSchema);

    std::string templatesDir(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-templates"));

    if ((dir = opendir(templatesDir.c_str())) == NULL)
        throw TConfigParserException("Cannot open " + templatesDir + " directory");

    while ((dirp = readdir(dir))) {
        std::string filepath = templatesDir + "/" + dirp->d_name;

        if (stat(filepath.c_str(), &filestat)) continue;
        if (S_ISDIR(filestat.st_mode)) continue;

        validator.Validate(WBMQTT::JSON::Parse(filepath));
    }
    closedir(dir);
}