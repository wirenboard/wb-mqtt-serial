#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "utils.h"

using namespace Utils;

using ::testing::ElementsAre;
using ::testing::_;

class UtilsTest : public ::testing::Test
{
public:
    void SetUp() {}
    void TearDown() {}
};

TEST_F(UtilsTest, ReadHexTest)
{
    uint8_t buffer[] = { 0x12, 0x34, 0x56 };

    EXPECT_EQ(ReadHex(buffer, 0), 0);

    EXPECT_EQ(ReadHex(buffer, sizeof (buffer)), 0x123456);
    EXPECT_EQ(ReadHex(buffer, 2), 0x1234);

    EXPECT_EQ(ReadHex(buffer, sizeof (buffer), false), 0x563412);
    EXPECT_EQ(ReadHex(buffer, 2, false), 0x3412);
}

TEST_F(UtilsTest, ReadBCDTest)
{
    uint8_t buffer[] = { 0x12, 0x34, 0x56 };

    EXPECT_EQ(ReadBCD(buffer, 0), 0);

    EXPECT_EQ(ReadBCD(buffer, sizeof (buffer)), 123456);
    EXPECT_EQ(ReadBCD(buffer, 2), 1234);

    EXPECT_EQ(ReadBCD(buffer, sizeof (buffer), false), 563412);
    EXPECT_EQ(ReadBCD(buffer, 2, false), 3412);
}

TEST_F(UtilsTest, WriteHexTest)
{
    uint8_t buffer[8];
    uint64_t val = 0x123456;

    WriteHex(0, buffer, sizeof (buffer));
    EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0, 0, 0, 0, 0));

    WriteHex(val, buffer, sizeof (buffer));
    EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0, 0, 0x12, 0x34, 0x56));

    WriteHex(val, buffer, 3);
    EXPECT_THAT(buffer, ElementsAre(0x12, 0x34, 0x56, _, _, _, _, _));

    WriteHex(val, buffer, sizeof (buffer), false);
    EXPECT_THAT(buffer, ElementsAre(0x56, 0x34, 0x12, 0, 0, 0, 0, 0));

    WriteHex(val, buffer, 3, false);
    EXPECT_THAT(buffer, ElementsAre(0x56, 0x34, 0x12, _, _, _, _, _));
}

TEST_F(UtilsTest, WriteBCDTest)
{
    uint8_t buffer[8];
    uint64_t val = 123456;

    WriteBCD(0, buffer, sizeof (buffer));
    EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0, 0, 0, 0, 0));

    WriteBCD(val, buffer, sizeof (buffer));
    EXPECT_THAT(buffer, ElementsAre(0, 0, 0, 0, 0, 0x12, 0x34, 0x56));

    WriteBCD(val, buffer, 3);
    EXPECT_THAT(buffer, ElementsAre(0x12, 0x34, 0x56, _, _, _, _, _));

    WriteBCD(val, buffer, sizeof (buffer), false);
    EXPECT_THAT(buffer, ElementsAre(0x56, 0x34, 0x12, 0, 0, 0, 0, 0));

    WriteBCD(val, buffer, 3, false);
    EXPECT_THAT(buffer, ElementsAre(0x56, 0x34, 0x12, _, _, _, _, _));
}
