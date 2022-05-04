#include "channel_value.h"
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
    TChannelValue registerValue{val};
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
    EXPECT_EQ(val & 0xffffffff, registerValue.Get<uint32_t>());
    EXPECT_EQ(val & 0xffff, registerValue.Get<uint16_t>());
    EXPECT_EQ(val & 0xff, registerValue.Get<uint8_t>());
}

TEST_F(RegisterValueTest, Set)
{
    TChannelValue registerValue;
    uint64_t val = 0x1122334455667788;
    registerValue.Set(val);
    EXPECT_EQ(val, registerValue.Get<uint64_t>());
}

TEST_F(RegisterValueTest, LeftShift)
{
    uint64_t val = 0x1122334455667788;
    TChannelValue registerValue{val};
    registerValue.PopWord();
    EXPECT_EQ(val >> 16, registerValue.Get<uint64_t>());
}

TEST_F(RegisterValueTest, PushWord)
{
    uint64_t val = 0xAABBCCDD;
    uint64_t valH = 0xAABB;
    uint64_t valL = 0xCCDD;
    TChannelValue registerValueRef{val};
    TChannelValue registerValue;
    registerValue.PushWord(valH);
    registerValue.PushWord(valL);
    EXPECT_EQ(registerValueRef, registerValue);
}

TEST_F(RegisterValueTest, rightShift)
{
    uint64_t val = 0xAABBCCDD;
    TChannelValue registerValue{val};

    for (uint32_t i = 0; i < 32; ++i) {
        auto result = registerValue >> i;
        EXPECT_EQ(val >> i, result.Get<uint64_t>());
    }
}

TEST_F(RegisterValueTest, String)
{
    TChannelValue value;
    std::string str = "abcdefgh1423";
    value.Set(str);
    EXPECT_EQ(str, value.Get<std::string>());
}

TEST_F(RegisterValueTest, ToString){
    TChannelValue value;
    uint64_t rawValue = 0xAABBCCDDEEFF1122;
    std::string str = "1122 eeff ccdd aabb ";
    value.Set(rawValue);
    EXPECT_EQ(str, value.ToString());
}