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
