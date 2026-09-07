#include "templates_map.h"
#include "test_utils.h"
#include <wblib/testing/testlog.h>

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    PTemplateMap MakeTemplateMap()
    {
        auto templateMap = std::make_shared<TTemplateMap>(GetTemplatesSchema());
        templateMap->AddTemplatesDir(TLoggedFixture::GetDataFilePath("device-templates"));
        return templateMap;
    }
}

TEST(TTemplateTitleTest, TranslationsById)
{
    auto deviceTemplate = MakeTemplateMap()->GetTemplate("translations_by_id");
    EXPECT_EQ("Device with translations by id", deviceTemplate->GetTitle());
    EXPECT_EQ("Устройство с переводами по id", deviceTemplate->GetTitle("ru"));
    EXPECT_NO_THROW(deviceTemplate->GetTemplate());
}

TEST(TTemplateTitleTest, TranslationsByTitle)
{
    auto deviceTemplate = MakeTemplateMap()->GetTemplate("translations_by_title");
    EXPECT_EQ("Device with translations by title", deviceTemplate->GetTitle());
    EXPECT_EQ("Устройство с переводами по title", deviceTemplate->GetTitle("ru"));
}

TEST(TTemplateTitleTest, IdHasPriorityOverTitle)
{
    auto deviceTemplate = MakeTemplateMap()->GetTemplate("translations_id_priority");
    EXPECT_EQ("Title by id", deviceTemplate->GetTitle());
    EXPECT_EQ("Название по id", deviceTemplate->GetTitle("ru"));
}

TEST(TTemplateTitleTest, NoTitleAndTranslations)
{
    auto deviceTemplate = MakeTemplateMap()->GetTemplate("MSU34");
    EXPECT_EQ("MSU34", deviceTemplate->GetTitle());
    EXPECT_EQ("MSU34", deviceTemplate->GetTitle("ru"));
}
