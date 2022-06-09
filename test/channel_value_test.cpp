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

//TEST_F(RegisterValueTest, orOperator)
//{
//    uint64_t val = 0xAABBCCDD;
//    uint64_t valOther = 0x1122334400000055;
//    TRegisterValue channelValue{val};
//    TRegisterValue otherChannelValue{valOther};
//    channelValue |= otherChannelValue;
//    EXPECT_EQ(val | valOther, channelValue.Get<uint64_t>());
//}

//TEST_F(RegisterValueTest, String)
//{
//    TRegisterValue value;
//    std::string str = "abcdefgh1423";
//    value.Set(str, 32);
//    EXPECT_EQ(str, value.Get<std::string>());
//
//    std::vector<uint16_t> vec = {'o', 'l', 'l', 'e', 'h'};
//    value.Set(vec);
//    EXPECT_EQ("hello", value.Get<std::string>());
//}

TEST_F(RegisterValueTest, ToString)
{
    TRegisterValue value;
    uint64_t rawValue = 0xAABBCCDDEEFF1122;
    std::string str = "22 11 ff ee dd cc bb aa ";
    value.Set(rawValue);
    EXPECT_EQ(str, value.ToString());
}