#include "common_utils.h"
#include "gtest/gtest.h"
#include <optional>

TEST(CommonUtilsTest, IsValidMqttTopicString)
{
    EXPECT_TRUE(util::IsValidMqttTopicString(""));
    EXPECT_TRUE(util::IsValidMqttTopicString(".:[]{}@%^&*!?-="));
    EXPECT_FALSE(util::IsValidMqttTopicString("+/'\"#$"));
    EXPECT_TRUE(util::IsValidMqttTopicString("abcd_efgh-1234 () "));
}

TEST(CommonUtilsTest, ConvertToMqttTopicValidString)
{
    EXPECT_EQ("Output frequency (_plus_|- Hz)", util::ConvertToValidMqttTopicString("Output frequency (+/- Hz)"));
    EXPECT_EQ("(_plus_|||__) .:[]{}@%^&*!?-=", util::ConvertToValidMqttTopicString("(+/'\"#$) .:[]{}@%^&*!?-="));
}

TEST(CommonUtilsTest, CompareVersionStrings)
{
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.2.3"), 0);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.2.2"), 1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.2.4"), -1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.3.3"), -1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "2.2.3"), -1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.2.3-rc2"), 1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3", "1.2.3+wb10"), -1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3-rc2", "1.2.3-rc1"), 1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3-rc2", "1.2.3+wb10"), -1);
    EXPECT_EQ(util::CompareVersionStrings("1.2.3+wb2", "1.2.3+wb10"), -1);
}
