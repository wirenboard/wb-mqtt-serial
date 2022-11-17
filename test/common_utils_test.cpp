#include "common_utils.h"
#include "gtest/gtest.h"
#include <optional>

TEST(CommonUtilsTest, ConvertToMqttTopicValidString)
{
    EXPECT_EQ("Output frequency (_plus_|- Hz)", util::ConvertToMqttTopicValidString("Output frequency (+/- Hz)"));
    EXPECT_EQ("(_plus_|||__) .:[]{}@%^&*!?-=", util::ConvertToMqttTopicValidString("(+/'\"#$) .:[]{}@%^&*!?-="));
}
