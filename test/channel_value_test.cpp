#include "register_value.h"
#include "gtest/gtest.h"
#include <experimental/optional>

TEST(RegisterValueTest, Get)
{
    uint64_t val = 0x1122334455667788;
    TRegisterValue registerValue{val};
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
    EXPECT_EQ(val & 0xffffffff, registerValue.Get<uint32_t>());
    EXPECT_EQ(val & 0xffff, registerValue.Get<uint16_t>());
    EXPECT_EQ(val & 0xff, registerValue.Get<uint8_t>());
}

TEST(RegisterValueTest, Set)
{
    TRegisterValue registerValue;
    uint64_t val = 0x1122334455667788;
    registerValue.Set(val);
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
}

TEST(RegisterValueTest, String)
{
    TRegisterValue value;
    std::string str = "abcdefgh1423";
    value.Set(str);
    EXPECT_EQ(str, value.Get<std::string>());
}