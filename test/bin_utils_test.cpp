#include "gtest/gtest.h"
#include "bin_utils.h"

TEST(BinUtilsTest, Get)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x78563412, BinUtils::Get<uint32_t>(bytes.begin(), bytes.end()));
}
#if 0 // enable after feature/59276-events
TEST(BinUtilsTest, GetFrom)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x78563412, BinUtils::GetFrom<uint32_t>(bytes.begin()));
}
#endif

TEST(BinUtilsTest, GetBigEndian)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x12345678, BinUtils::GetBigEndian<uint32_t>(bytes.begin(), bytes.end()));
}
#if 0 // enable after feature/59276-events
TEST(BinUtilsTest, GetFromBigEndian)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x12345678, BinUtils::GetFromBigEndian<uint32_t>(bytes.begin()));
}
#endif
TEST(BinUtilsTest, Append)
{
    std::vector<uint8_t> out;
    BinUtils::Append(std::back_inserter(out), 0x12345678, 4);
    EXPECT_EQ(out.size(), 4);
    EXPECT_EQ(0x78, out[0]);
    EXPECT_EQ(0x56, out[1]);
    EXPECT_EQ(0x34, out[2]);
    EXPECT_EQ(0x12, out[3]);
}
#if 0 // enable after feature/59276-events
TEST(BinUtilsTest, AppendUint8)
{
    std::vector<uint8_t> out;
    BinUtils::Append(std::back_inserter(out), {0x78, 0x56, 0x34});
    EXPECT_EQ(out.size(), 3);
    EXPECT_EQ(0x78, out[0]);
    EXPECT_EQ(0x56, out[1]);
    EXPECT_EQ(0x34, out[2]);
}

TEST(BinUtilsTest, AppendBigEndian)
{
    std::vector<uint8_t> out;
    BinUtils::AppendBigEndian(std::back_inserter(out), 0x12345678, 4);
    EXPECT_EQ(out.size(), 4);
    EXPECT_EQ(0x12, out[0]);
    EXPECT_EQ(0x34, out[1]);
    EXPECT_EQ(0x56, out[2]);
    EXPECT_EQ(0x78, out[3]);
}
#endif
TEST(BinUtilsTest, GetLSBMask)
{
    EXPECT_EQ(0x01, BinUtils::GetLSBMask(1));
    EXPECT_EQ(0x03, BinUtils::GetLSBMask(2));
    EXPECT_EQ(0x07, BinUtils::GetLSBMask(3));
    EXPECT_EQ(0x0f, BinUtils::GetLSBMask(4));
    EXPECT_EQ(0x1f, BinUtils::GetLSBMask(5));
}
