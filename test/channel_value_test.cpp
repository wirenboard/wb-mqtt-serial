#include "register_value.h"
#include "gtest/gtest.h"
#include <experimental/optional>

class RegisterValueTest: public ::testing::Test
{
protected:
    void SetUp() override
    {}

    void TearDown() override
    {}
};

TEST_F(RegisterValueTest, Get)
{
    uint64_t val = 0x1122334455667788;
    TRegisterValue registerValue{val};
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
    EXPECT_EQ(val & 0xffffffff, registerValue.Get<uint32_t>());
    EXPECT_EQ(val & 0xffff, registerValue.Get<uint16_t>());
    EXPECT_EQ(val & 0xff, registerValue.Get<uint8_t>());
}

TEST_F(RegisterValueTest, Set)
{
    TRegisterValue registerValue;
    uint64_t val = 0x1122334455667788;
    registerValue.Set(val);
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
}

TEST_F(RegisterValueTest, String)
{
    TRegisterValue value;
    std::string str = "abcdefgh1423";
    value.Set(str);
    EXPECT_EQ(str, value.Get<std::string>());
}