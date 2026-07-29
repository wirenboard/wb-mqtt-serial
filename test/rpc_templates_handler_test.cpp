#include <algorithm>
#include <filesystem>
#include <fstream>

#include <wblib/testing/testlog.h>

#include "rpc/rpc_templates_handler.h"
#include "serial_config.h"
#include "test_utils.h"

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    const std::string TEMPLATE_IN_USE_ERROR = "template-in-use";
    const std::string USED_TYPE = "MSU34";               // present in the test config
    const std::string STOCK_TYPE = "TestDeviceOverride"; // present only in stock templates
    const std::string NEW_TYPE = "UploadedDevice";

    std::string SerializeJson(const Json::Value& value)
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        return Json::writeString(builder, value);
    }

    Json::Value MakeTemplateJson(const std::string& deviceType,
                                 const std::string& title,
                                 const std::string& group = std::string())
    {
        Json::Value root;
        root["device_type"] = deviceType;
        root["title"] = title;
        if (!group.empty()) {
            root["group"] = group;
        }
        root["device"]["name"] = title;
        root["device"]["id"] = "test-uploaded-device";
        Json::Value channel;
        channel["name"] = "Input 1";
        channel["reg_type"] = "input";
        channel["address"] = 0;
        root["device"]["channels"].append(channel);
        return root;
    }

    std::string MakeTemplateContent(const std::string& deviceType,
                                    const std::string& title,
                                    const std::string& group = std::string())
    {
        return SerializeJson(MakeTemplateJson(deviceType, title, group));
    }

    Json::Value MakeUploadRequest(const std::string& content,
                                  const std::string& filename,
                                  bool force = false,
                                  const std::string& lang = std::string())
    {
        Json::Value request;
        request["content"] = content;
        request["filename"] = filename;
        if (force) {
            request["force"] = true;
        }
        if (!lang.empty()) {
            request["lang"] = lang;
        }
        return request;
    }

    Json::Value MakeDeleteRequest(const std::string& type, bool force = false)
    {
        Json::Value request;
        request["type"] = type;
        if (force) {
            request["force"] = true;
        }
        return request;
    }
}

class TRPCTemplatesHandlerTest: public testing::Test
{
protected:
    std::filesystem::path UserTemplatesDir;
    Json::Value CommonDeviceSchema;
    TSerialDeviceFactory DeviceFactory;
    PTemplateMap Templates;
    std::unique_ptr<TDevicesConfedSchemasMap> ConfedSchemas;
    std::unique_ptr<TRPCTemplatesHandler> Handler;

    void SetUp() override
    {
        UserTemplatesDir = std::filesystem::temp_directory_path() / "wb-mqtt-serial-user-templates-test" /
                           ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(UserTemplatesDir);
        std::filesystem::create_directories(UserTemplatesDir);

        RegisterProtocols(DeviceFactory);
        CommonDeviceSchema = GetCommonDeviceSchema();
        Templates = std::make_shared<TTemplateMap>(GetTemplatesSchema(), UserTemplatesDir.string());
        Templates->AddTemplatesDir(TLoggedFixture::GetDataFilePath("device-templates"));
        Templates->AddTemplatesDir(UserTemplatesDir.string());
        ConfedSchemas = std::make_unique<TDevicesConfedSchemasMap>(*Templates, DeviceFactory, CommonDeviceSchema);
        MakeHandler(TLoggedFixture::GetDataFilePath("configs/rpc-templates-handler-test.json"));
    }

    void MakeHandler(const std::string& configPath)
    {
        Handler.reset(new TRPCTemplatesHandler(
            UserTemplatesDir.string(),
            configPath,
            Templates,
            *ConfedSchemas,
            WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../groups.json")),
            TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-rpc-templates-upload-request.schema.json"),
            TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-rpc-templates-delete-request.schema.json")));
    }

    void TearDown() override
    {
        std::filesystem::remove_all(UserTemplatesDir);
    }

    Json::Value Upload(const Json::Value& request)
    {
        return Handler->UploadTemplate(request);
    }

    Json::Value Delete(const Json::Value& request)
    {
        return Handler->DeleteTemplate(request);
    }

    std::vector<std::string> ListUserTemplatesDir() const
    {
        std::vector<std::string> res;
        for (const auto& entry: std::filesystem::directory_iterator(UserTemplatesDir)) {
            res.push_back(entry.path().filename().string());
        }
        std::sort(res.begin(), res.end());
        return res;
    }

    //! Checks {"types": [{"name": <groupName>, "types": [<type description>]}]} layout and returns type description
    Json::Value GetSingleType(const Json::Value& response, const std::string& groupName) const
    {
        EXPECT_TRUE(response["types"].isArray());
        EXPECT_EQ(1u, response["types"].size());
        EXPECT_EQ(groupName, response["types"][0]["name"].asString());
        EXPECT_TRUE(response["types"][0]["types"].isArray());
        EXPECT_EQ(1u, response["types"][0]["types"].size());
        return response["types"][0]["types"][0];
    }
};

TEST_F(TRPCTemplatesHandlerTest, UploadNewType)
{
    auto response = Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));

    EXPECT_EQ(std::vector<std::string>{"uploaded-device.json"}, ListUserTemplatesDir());
    // Template without a group is shown in the custom group
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(NEW_TYPE, typeJson["type"].asString());
    EXPECT_EQ("Uploaded device", typeJson["name"].asString());
    EXPECT_TRUE(typeJson["user-defined"].asBool());
    EXPECT_FALSE(typeJson["deprecated"].asBool());
    EXPECT_EQ("modbus", typeJson["protocol"].asString());

    // The type is available for config editor already
    auto deviceTemplate = Templates->GetTemplate(NEW_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ((UserTemplatesDir / "uploaded-device.json").string(), deviceTemplate->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, UploadNewTypeTranslatedGroupName)
{
    auto response = Upload(
        MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json", false, "ru"));
    GetSingleType(response, "Произвольные устройства");
}

TEST_F(TRPCTemplatesHandlerTest, ReUploadSameTypeSameFilename)
{
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));
    auto response =
        Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device v2"), "uploaded-device.json"));

    EXPECT_EQ(std::vector<std::string>{"uploaded-device.json"}, ListUserTemplatesDir());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ("Uploaded device v2", typeJson["name"].asString());
    EXPECT_EQ("Uploaded device v2", Templates->GetTemplate(NEW_TYPE)->GetTitle());
}

TEST_F(TRPCTemplatesHandlerTest, ReUploadSameTypeDifferentFilename)
{
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device v2"), "renamed-device.json"));

    // Old file is removed, there is only one template for the type
    EXPECT_EQ(std::vector<std::string>{"renamed-device.json"}, ListUserTemplatesDir());
    auto deviceTemplate = Templates->GetTemplate(NEW_TYPE);
    EXPECT_EQ("Uploaded device v2", deviceTemplate->GetTitle());
    EXPECT_EQ((UserTemplatesDir / "renamed-device.json").string(), deviceTemplate->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, ReUploadKeepsMapEntryWhenOldFileRemovalFails)
{
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));
    auto oldPath = UserTemplatesDir / "uploaded-device.json";
    // Make the old file unremovable: replace it with a non-empty directory
    std::filesystem::remove(oldPath);
    std::filesystem::create_directories(oldPath / "sub");

    auto response =
        Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device v2"), "renamed-device.json"));

    // Upload succeeds, the new template is active
    EXPECT_EQ("Uploaded device v2", GetSingleType(response, "Custom devices")["name"].asString());
    auto newPath = UserTemplatesDir / "renamed-device.json";
    EXPECT_EQ(newPath.string(), Templates->GetTemplate(NEW_TYPE)->GetFilePath());
    EXPECT_EQ(std::vector<std::string>({"renamed-device.json", "uploaded-device.json"}), ListUserTemplatesDir());

    // The map keeps the entry for the old path while it is still present on disk
    Templates->DeleteTemplate(newPath.string());
    auto stale = Templates->FindUserDefinedTemplate(NEW_TYPE);
    ASSERT_TRUE(stale);
    EXPECT_EQ(oldPath.string(), stale->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, UploadSanitizesFilename)
{
    // Only the basename is used, the file can't be written outside of the user templates directory
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "../uploaded.json"));
    EXPECT_EQ(std::vector<std::string>{"uploaded.json"}, ListUserTemplatesDir());

    // ".json" suffix is added if missing (old file of the type is replaced)
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "/etc/passwd"));
    EXPECT_EQ(std::vector<std::string>{"passwd.json"}, ListUserTemplatesDir());

    // Characters not from [A-Za-z0-9._-] are replaced with '_'
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "we ird&:name.json"));
    EXPECT_EQ(std::vector<std::string>{"we_ird__name.json"}, ListUserTemplatesDir());
}

TEST_F(TRPCTemplatesHandlerTest, UploadInvalidFilename)
{
    // A filename without a usable basename is rejected, nothing is written to the user templates directory
    EXPECT_THROW(Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "/")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());

    EXPECT_THROW(Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), ".json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());
}

TEST_F(TRPCTemplatesHandlerTest, UploadUsedTypeWithoutForce)
{
    try {
        Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json"));
        FAIL() << "Upload of a used type without \"force\" must throw";
    } catch (const std::exception& e) {
        EXPECT_EQ(TEMPLATE_IN_USE_ERROR, std::string(e.what()));
    }
    EXPECT_TRUE(ListUserTemplatesDir().empty());
}

TEST_F(TRPCTemplatesHandlerTest, UploadUsedTypeWithForce)
{
    auto response = Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json", true));

    EXPECT_EQ(std::vector<std::string>{"msu34.json"}, ListUserTemplatesDir());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(USED_TYPE, typeJson["type"].asString());
    EXPECT_TRUE(typeJson["user-defined"].asBool());
    EXPECT_TRUE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesHandlerTest, UploadUsedTypeWithMissingConfig)
{
    // A missing (or unreadable) config can't refer to the template, so nothing is protected
    // and "force" is not required
    MakeHandler((UserTemplatesDir / "nonexistent-config.json").string());

    auto response = Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json"));

    EXPECT_EQ(std::vector<std::string>{"msu34.json"}, ListUserTemplatesDir());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(USED_TYPE, typeJson["type"].asString());
    EXPECT_TRUE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesHandlerTest, UploadInvalidatesSchemaCache)
{
    auto schemaBefore = ConfedSchemas->GetSchema(STOCK_TYPE);

    Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device"), "override.json"));

    auto schemaAfter = ConfedSchemas->GetSchema(STOCK_TYPE);
    EXPECT_NE(schemaBefore.get(), schemaAfter.get());
}

TEST_F(TRPCTemplatesHandlerTest, OverrideUnusedStockType)
{
    auto stockPath = TLoggedFixture::GetDataFilePath("device-templates/test-device-override.json");
    std::ifstream stockFileBefore(stockPath);
    std::string stockContentBefore((std::istreambuf_iterator<char>(stockFileBefore)), std::istreambuf_iterator<char>());

    auto response =
        Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device", "g-io"), "override.json"));

    // Stock template file is not modified, the user defined template is active
    std::ifstream stockFileAfter(stockPath);
    std::string stockContentAfter((std::istreambuf_iterator<char>(stockFileAfter)), std::istreambuf_iterator<char>());
    EXPECT_EQ(stockContentBefore, stockContentAfter);

    auto typeJson = GetSingleType(response, "IO modules");
    EXPECT_EQ(STOCK_TYPE, typeJson["type"].asString());
    EXPECT_TRUE(typeJson["user-defined"].asBool());
    auto deviceTemplate = Templates->GetTemplate(STOCK_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ("Overridden device", deviceTemplate->GetTitle());
}

TEST_F(TRPCTemplatesHandlerTest, OverrideChangesGroup)
{
    auto response =
        Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded", "g-relay"), "msu34.json", true));
    GetSingleType(response, "Relays");

    // A template without a group moves the type to the custom group
    response = Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json", true));
    GetSingleType(response, "Custom devices");
}

TEST_F(TRPCTemplatesHandlerTest, UploadFilenameCollisionWithOtherType)
{
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "shared.json"));
    Upload(MakeUploadRequest(MakeTemplateContent("AnotherUploadedDevice", "Another device"), "shared.json"));

    // The file of another type is not overwritten, a numeric suffix is added
    EXPECT_EQ((std::vector<std::string>{"shared-1.json", "shared.json"}), ListUserTemplatesDir());
    EXPECT_EQ((UserTemplatesDir / "shared.json").string(), Templates->GetTemplate(NEW_TYPE)->GetFilePath());
    EXPECT_EQ((UserTemplatesDir / "shared-1.json").string(),
              Templates->GetTemplate("AnotherUploadedDevice")->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, UploadInvalidContent)
{
    // Broken JSON
    EXPECT_THROW(Upload(MakeUploadRequest("{ broken", "broken.json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());

    // Missing device_type
    Json::Value withoutType;
    withoutType["device"]["name"] = "No type";
    EXPECT_THROW(Upload(MakeUploadRequest(SerializeJson(withoutType), "no-type.json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());

    // Schema violation: required "device" property is missing
    Json::Value withoutDevice;
    withoutDevice["device_type"] = NEW_TYPE;
    EXPECT_THROW(Upload(MakeUploadRequest(SerializeJson(withoutDevice), "no-device.json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());
}

TEST_F(TRPCTemplatesHandlerTest, UploadDeprecatedTemplateIsRejected)
{
    // Deprecated templates skip schema validation on disk load,
    // so uploading them is not supported at all
    auto root = MakeTemplateJson("DeprecatedUploadedDevice", "Deprecated uploaded device");
    root["deprecated"] = true;

    EXPECT_THROW(Upload(MakeUploadRequest(SerializeJson(root), "deprecated.json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_THROW(Templates->GetTemplate("DeprecatedUploadedDevice"), std::out_of_range);
}

TEST_F(TRPCTemplatesHandlerTest, UploadMalformedMetadata)
{
    // Values that break template metadata parsing must be rejected by the JSON schema
    // before anything is written: "device" must be an object, "group" must be a string,
    // the whole template must be a JSON object
    auto nonObjectDevice = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    nonObjectDevice["device"] = "not-an-object-or-array";
    auto nonStringGroup = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    nonStringGroup["group"]["not"] = "a string";

    for (const auto& content: {SerializeJson(nonObjectDevice), SerializeJson(nonStringGroup), std::string("[1, 2]")}) {
        EXPECT_THROW(Upload(MakeUploadRequest(content, "malformed.json")), std::exception);
        // No file and no leftover *.tmp in the user templates directory
        EXPECT_TRUE(ListUserTemplatesDir().empty());
        EXPECT_THROW(Templates->GetTemplate(NEW_TYPE), std::out_of_range);
    }

    // A previously uploaded good template survives a malformed re-upload with the same filename
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));
    EXPECT_THROW(Upload(MakeUploadRequest(SerializeJson(nonObjectDevice), "uploaded-device.json")), std::exception);

    EXPECT_EQ(std::vector<std::string>{"uploaded-device.json"}, ListUserTemplatesDir());
    auto deviceTemplate = Templates->GetTemplate(NEW_TYPE);
    EXPECT_EQ("Uploaded device", deviceTemplate->GetTitle());
    EXPECT_EQ((UserTemplatesDir / "uploaded-device.json").string(), deviceTemplate->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, UploadInvalidCondition)
{
    // A schema-valid template with a broken "condition" expression must be rejected
    // by the same checks that run on first template use, before anything is written
    auto root = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    root["device"]["channels"][0]["condition"] = "a b";

    EXPECT_THROW(Upload(MakeUploadRequest(SerializeJson(root), "invalid-condition.json")), std::exception);
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_THROW(Templates->GetTemplate(NEW_TYPE), std::out_of_range);
}

TEST_F(TRPCTemplatesHandlerTest, DeleteOverride)
{
    Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded", "g-relay"), "msu34.json", true));
    auto schemaBefore = ConfedSchemas->GetSchema(USED_TYPE);

    auto response = Delete(MakeDeleteRequest(USED_TYPE, true));

    // The type falls back to the stock template and to the stock template group
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(USED_TYPE, typeJson["type"].asString());
    EXPECT_FALSE(typeJson.isMember("user-defined"));
    EXPECT_FALSE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());

    // Schema cache is invalidated
    auto schemaAfter = ConfedSchemas->GetSchema(USED_TYPE);
    EXPECT_NE(schemaBefore.get(), schemaAfter.get());
}

TEST_F(TRPCTemplatesHandlerTest, DeleteSoleUserType)
{
    Upload(MakeUploadRequest(MakeTemplateContent(NEW_TYPE, "Uploaded device"), "uploaded-device.json"));

    auto response = Delete(MakeDeleteRequest(NEW_TYPE));

    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_TRUE(response["types"].isArray());
    EXPECT_EQ(0u, response["types"].size());
    EXPECT_THROW(Templates->GetTemplate(NEW_TYPE), std::out_of_range);

    // Repeated delete fails without a crash
    EXPECT_THROW(Delete(MakeDeleteRequest(NEW_TYPE)), std::exception);
}

TEST_F(TRPCTemplatesHandlerTest, DeleteUsedTypeWithoutForce)
{
    Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json", true));

    try {
        Delete(MakeDeleteRequest(USED_TYPE));
        FAIL() << "Delete of a used type without \"force\" must throw";
    } catch (const std::exception& e) {
        EXPECT_EQ(TEMPLATE_IN_USE_ERROR, std::string(e.what()));
    }
    // The file is intact, the user defined template is still active
    EXPECT_EQ(std::vector<std::string>{"msu34.json"}, ListUserTemplatesDir());
    EXPECT_TRUE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesHandlerTest, DeleteUsedTypeWithForce)
{
    Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json", true));

    Delete(MakeDeleteRequest(USED_TYPE, true));

    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_FALSE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());
}

TEST_F(TRPCTemplatesHandlerTest, DeleteTypeWithoutUserTemplate)
{
    // Stock only type
    EXPECT_THROW(Delete(MakeDeleteRequest(STOCK_TYPE)), std::exception);
    // Unknown type
    EXPECT_THROW(Delete(MakeDeleteRequest("UnknownDeviceType")), std::exception);
}
