#include "templates_map.h"
#include "test_utils.h"
#include <wblib/testing/testlog.h>

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    const std::string OVERRIDDEN_TYPE = "MSU34";
    const std::string USER_ONLY_TYPE = "UserOnlyDevice";
    const std::string STOCK_ONLY_TYPE = "TestDeviceOverride";

    PTemplateMap MakeTemplateMap(bool withUserTemplatesDir)
    {
        auto templateMap = std::make_shared<TTemplateMap>(
            GetTemplatesSchema(),
            withUserTemplatesDir ? TLoggedFixture::GetDataFilePath("user-templates") : std::string());
        templateMap->AddTemplatesDir(TLoggedFixture::GetDataFilePath("device-templates"));
        templateMap->AddTemplatesDir(TLoggedFixture::GetDataFilePath("user-templates"));
        return templateMap;
    }
}

TEST(TUserTemplatesTest, UserTemplateOverridesStockType)
{
    auto templateMap = MakeTemplateMap(true);
    auto deviceTemplate = templateMap->GetTemplate(OVERRIDDEN_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    // The template from user templates directory must be preferred
    EXPECT_EQ("MSU34 user", deviceTemplate->GetTitle());
    EXPECT_EQ(TLoggedFixture::GetDataFilePath("user-templates/msu34-user.json"), deviceTemplate->GetFilePath());
    auto userTemplate = templateMap->FindUserDefinedTemplate(OVERRIDDEN_TYPE);
    ASSERT_TRUE(userTemplate);
    EXPECT_EQ(deviceTemplate, userTemplate);
}

TEST(TUserTemplatesTest, UserTemplateWithNewType)
{
    auto templateMap = MakeTemplateMap(true);
    auto deviceTemplate = templateMap->GetTemplate(USER_ONLY_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ(deviceTemplate, templateMap->FindUserDefinedTemplate(USER_ONLY_TYPE));
}

TEST(TUserTemplatesTest, StockTemplateIsNotUserDefined)
{
    auto templateMap = MakeTemplateMap(true);
    EXPECT_FALSE(templateMap->GetTemplate(STOCK_ONLY_TYPE)->IsUserDefined());
    EXPECT_FALSE(templateMap->FindUserDefinedTemplate(STOCK_ONLY_TYPE));
}

TEST(TUserTemplatesTest, NoUserTemplatesDir)
{
    auto templateMap = MakeTemplateMap(false);
    EXPECT_FALSE(templateMap->GetTemplate(OVERRIDDEN_TYPE)->IsUserDefined());
    EXPECT_FALSE(templateMap->GetTemplate(USER_ONLY_TYPE)->IsUserDefined());
    EXPECT_FALSE(templateMap->GetTemplate(STOCK_ONLY_TYPE)->IsUserDefined());
    EXPECT_FALSE(templateMap->FindUserDefinedTemplate(OVERRIDDEN_TYPE));
}

TEST(TUserTemplatesTest, UpdateTemplateKeepsUserDefinedFlag)
{
    auto templateMap = MakeTemplateMap(true);

    // Live reload of a file from the user templates directory
    templateMap->UpdateTemplate(TLoggedFixture::GetDataFilePath("user-templates/msu34-user.json"));
    EXPECT_TRUE(templateMap->GetTemplate(OVERRIDDEN_TYPE)->IsUserDefined());

    // Live reload of a file from the stock templates directory
    templateMap->UpdateTemplate(TLoggedFixture::GetDataFilePath("device-templates/test-device-override.json"));
    EXPECT_FALSE(templateMap->GetTemplate(STOCK_ONLY_TYPE)->IsUserDefined());
}
