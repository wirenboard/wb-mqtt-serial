#include <algorithm>
#include <filesystem>
#include <fstream>

#include <wblib/testing/testlog.h>

#include "rpc/rpc_config_change_handler.h"
#include "rpc/rpc_helpers.h"
#include "rpc_service_test_utils.h"
#include "serial_config.h"
#include "test_utils.h"

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    const std::string USED_TYPE = "MSU34";               // present in the test config
    const std::string STOCK_TYPE = "TestDeviceOverride"; // present only in stock templates
    const std::string NEW_TYPE = "UploadedDevice";

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
    std::filesystem::path TestDir;
    std::filesystem::path UserTemplatesDir;
    std::string ConfigPath;
    PTemplateMap Templates;
    std::unique_ptr<TSerialDriverCore> SerialDriverCore;
    std::unique_ptr<TRPCConfigChangeHandler> Handler;
    std::shared_ptr<TRPCMethods> Rpc;

    void SetUp() override
    {
        TestDir = std::filesystem::temp_directory_path() / "wb-mqtt-serial-user-templates-test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name();
        UserTemplatesDir = TestDir / "templates";
        std::filesystem::remove_all(TestDir);
        std::filesystem::create_directories(UserTemplatesDir);
        // The handler may write the configuration
        ConfigPath = (TestDir / "wb-mqtt-serial.conf").string();
        std::filesystem::copy_file(TLoggedFixture::GetDataFilePath("configs/rpc-templates-handler-test.json"),
                                   ConfigPath);
        MakeHandler(ConfigPath);
    }

    //! The service with the configuration file
    void MakeHandler(const std::string& configPath)
    {
        if (Rpc) {
            Rpc->Stop();
        }
        Handler.reset();
        Templates.reset();
        SerialDriverCore.reset();
        SerialDriverCore = std::make_unique<TSerialDriverCore>(
            MakeUnconnectedMqttDriver(),
            MakeSerialDriverCoreFiles(configPath,
                                      TLoggedFixture::GetDataFilePath("device-templates"),
                                      UserTemplatesDir.string()),
            false);
        Templates = SerialDriverCore->GetTemplates();
        Handler.reset(new TRPCConfigChangeHandler(
            WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../groups.json")),
            TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-rpc-templates-upload-request.schema.json"),
            TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-rpc-templates-delete-request.schema.json"),
            *SerialDriverCore));
        Rpc = std::make_shared<TRPCMethods>();
        RegisterConfigChangeRpcHandlers(*Handler, Rpc);
    }

    void TearDown() override
    {
        Rpc->Stop();
        Handler.reset();
        Templates.reset();
        SerialDriverCore.reset();
        std::filesystem::remove_all(TestDir);
    }

    //! @throws std::runtime_error with "error.data" of a refused request
    Json::Value Call(const std::string& method, const Json::Value& request)
    {
        auto answer = CallAsync(*Rpc, method, request);
        auto error = answer->Get();
        if (!error.empty()) {
            throw std::runtime_error(error);
        }
        return answer->GetResult();
    }

    Json::Value Upload(const Json::Value& request)
    {
        return Call("templates/Upload", request);
    }

    Json::Value Delete(const Json::Value& request)
    {
        return Call("templates/Delete", request);
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

    // The configuration of an unused type is the current one
    EXPECT_EQ(WBMQTT::JSON::Parse(ConfigPath), response["config"]);

    // The type is available for config editor already
    auto deviceTemplate = Templates->GetTemplate(NEW_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ((UserTemplatesDir / "uploaded-device.json").string(), deviceTemplate->GetFilePath());
}

TEST_F(TRPCTemplatesHandlerTest, UploadTypeWithParameterFwVariants)
{
    auto templateJson = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    Json::Value parameter;
    parameter["id"] = "p1";
    parameter["title"] = "p1";
    parameter["address"] = 10;
    parameter["enum"].append(0);
    parameter["enum"].append(1);
    parameter["enum_titles"].append("a");
    parameter["enum_titles"].append("b");
    templateJson["device"]["parameters"].append(parameter);
    parameter["fw"] = "2.2.0";
    parameter["enum"].append(2);
    parameter["enum_titles"].append("c");
    templateJson["device"]["parameters"].append(parameter);

    auto response = Upload(MakeUploadRequest(SerializeJson(templateJson), "uploaded-device.json"));
    GetSingleType(response, "Custom devices");
    EXPECT_NO_THROW(Templates->GetTemplate(NEW_TYPE)->GetTemplate());
}

TEST_F(TRPCTemplatesHandlerTest, UploadParameterFwVariantsMismatch)
{
    // A schema-valid template where fw variants of a parameter differ not only in enum values
    // must be rejected by the same check as on template load
    auto templateJson = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    Json::Value parameter;
    parameter["id"] = "p1";
    parameter["title"] = "p1";
    parameter["address"] = 10;
    parameter["scale"] = 1;
    parameter["enum"].append(0);
    parameter["enum"].append(1);
    parameter["enum_titles"].append("a");
    parameter["enum_titles"].append("b");
    templateJson["device"]["parameters"].append(parameter);
    parameter["fw"] = "2.2.0";
    parameter["scale"] = 2;
    parameter["enum"].append(2);
    parameter["enum_titles"].append("c");
    templateJson["device"]["parameters"].append(parameter);

    try {
        Upload(MakeUploadRequest(SerializeJson(templateJson), "uploaded-device.json"));
        ADD_FAILURE() << "Expect an exception";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("Parameter \"p1\" has several declarations with different \"scale\" "
                                             "values (\"1\" and \"2\")."),
                  std::string::npos)
            << e.what();
    }
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_THROW(Templates->GetTemplate(NEW_TYPE), std::out_of_range);
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

TEST_F(TRPCTemplatesHandlerTest, ReUploadSucceedsWhenOldFileRemovalFails)
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

TEST_F(TRPCTemplatesHandlerTest, UploadUsedTypeWithMissingConfig)
{
    // A missing (or unreadable) config can't refer to the template, so nothing is protected
    // and "force" is not required
    MakeHandler((TestDir / "nonexistent-config.json").string());

    auto response = Upload(MakeUploadRequest(MakeTemplateContent(USED_TYPE, "MSU34 uploaded"), "msu34.json"));

    EXPECT_EQ(std::vector<std::string>{"msu34.json"}, ListUserTemplatesDir());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(USED_TYPE, typeJson["type"].asString());
    EXPECT_TRUE(Templates->GetTemplate(USED_TYPE)->IsUserDefined());
    EXPECT_TRUE(response["config"].isNull());
}

TEST_F(TRPCTemplatesHandlerTest, UploadInvalidatesSchemaCache)
{
    auto schemaBefore = SerialDriverCore->GetDeviceConfedSchemas().GetSchema(STOCK_TYPE);

    Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device"), "override.json"));

    auto schemaAfter = SerialDriverCore->GetDeviceConfedSchemas().GetSchema(STOCK_TYPE);
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
        Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device", "g-relay"), "override.json"));
    GetSingleType(response, "Relays");

    // A template without a group moves the type to the custom group
    response = Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device"), "override.json"));
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

TEST_F(TRPCTemplatesHandlerTest, UploadConditionSourceParameterMismatch)
{
    // A schema-valid template where declarations of a parameter used in conditions
    // differ in register reading and conversion properties must be rejected
    auto root = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    root["device"]["channels"][0]["condition"] = "mode==1";
    Json::Value sel;
    sel["id"] = "sel";
    sel["title"] = "sel";
    sel["address"] = 1;
    root["device"]["parameters"].append(sel);
    Json::Value mode;
    mode["id"] = "mode";
    mode["title"] = "mode";
    mode["address"] = 2;
    mode["scale"] = 1;
    mode["condition"] = "sel==0";
    root["device"]["parameters"].append(mode);
    mode["scale"] = 2;
    mode["condition"] = "sel==1";
    root["device"]["parameters"].append(mode);

    try {
        Upload(MakeUploadRequest(SerializeJson(root), "condition-source.json"));
        ADD_FAILURE() << "Expect an exception";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("Parameter \"mode\" has several declarations with different \"scale\" "
                                             "values (\"1\" and \"2\")."),
                  std::string::npos)
            << e.what();
    }
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    EXPECT_THROW(Templates->GetTemplate(NEW_TYPE), std::out_of_range);
}

TEST_F(TRPCTemplatesHandlerTest, UploadNumericChannelEnum)
{
    // Templates use numeric channel enums, the schema allows strings only,
    // so the values must be converted before validation, as it is done on disk load
    auto root = MakeTemplateJson(NEW_TYPE, "Uploaded device");
    auto& channel = root["device"]["channels"][0];
    channel["enum"].append(0);
    channel["enum"].append(1);
    channel["enum_titles"].append("off");
    channel["enum_titles"].append("on");

    Upload(MakeUploadRequest(SerializeJson(root), "numeric-enum.json"));

    EXPECT_EQ(std::vector<std::string>{"numeric-enum.json"}, ListUserTemplatesDir());
    // The file keeps the original values, the loaded template has them converted
    auto stored = WBMQTT::JSON::Parse((UserTemplatesDir / "numeric-enum.json").string());
    EXPECT_TRUE(stored["device"]["channels"][0]["enum"][0].isNumeric());
    auto loaded = Templates->GetTemplate(NEW_TYPE)->GetTemplate();
    EXPECT_EQ("0", loaded["channels"][0]["enum"][0].asString());
    EXPECT_EQ("1", loaded["channels"][0]["enum"][1].asString());
}

TEST_F(TRPCTemplatesHandlerTest, DeleteOverride)
{
    Upload(MakeUploadRequest(MakeTemplateContent(STOCK_TYPE, "Overridden device", "g-relay"), "override.json"));
    auto schemaBefore = SerialDriverCore->GetDeviceConfedSchemas().GetSchema(STOCK_TYPE);

    auto response = Delete(MakeDeleteRequest(STOCK_TYPE));

    // The type falls back to the stock template and to the stock template group
    EXPECT_TRUE(ListUserTemplatesDir().empty());
    auto typeJson = GetSingleType(response, "Custom devices");
    EXPECT_EQ(STOCK_TYPE, typeJson["type"].asString());
    EXPECT_FALSE(typeJson.isMember("user-defined"));
    EXPECT_FALSE(Templates->GetTemplate(STOCK_TYPE)->IsUserDefined());

    // Schema cache is invalidated
    auto schemaAfter = SerialDriverCore->GetDeviceConfedSchemas().GetSchema(STOCK_TYPE);
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

TEST_F(TRPCTemplatesHandlerTest, DeleteTypeWithoutUserTemplate)
{
    // Stock only type
    EXPECT_THROW(Delete(MakeDeleteRequest(STOCK_TYPE)), std::exception);
    // Unknown type
    EXPECT_THROW(Delete(MakeDeleteRequest("UnknownDeviceType")), std::exception);
}
