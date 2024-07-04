#include "bin_utils.h"
#include "gtest/gtest.h"

TEST(BinUtilsTest, Get)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x78563412, BinUtils::Get<uint32_t>(bytes.begin(), bytes.end()));
}

TEST(BinUtilsTest, GetFrom)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x78563412, BinUtils::GetFrom<uint32_t>(bytes.begin()));
}

TEST(BinUtilsTest, GetBigEndian)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x12345678, BinUtils::GetBigEndian<uint32_t>(bytes.begin(), bytes.end()));
}

TEST(BinUtilsTest, GetFromBigEndian)
{
    const std::vector<uint8_t> bytes = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(0x12345678, BinUtils::GetFromBigEndian<uint32_t>(bytes.begin()));
}

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

TEST(BinUtilsTest, GetLSBMask)
{
    EXPECT_EQ(0x01, BinUtils::GetLSBMask(1));
    EXPECT_EQ(0x03, BinUtils::GetLSBMask(2));
    EXPECT_EQ(0x07, BinUtils::GetLSBMask(3));
    EXPECT_EQ(0x0f, BinUtils::GetLSBMask(4));
    EXPECT_EQ(0x1f, BinUtils::GetLSBMask(5));
}

TEST(BinUtilsTest, ApplyByteStuffing)
{
    const std::unordered_map<uint8_t, std::vector<uint8_t>> rules = {{0xc0, {0xdb, 0xdc}}, {0xdb, {0xdb, 0xdd}}};
    std::vector<uint8_t> data =
        {0x48, 0xfd, 0x00, 0xff, 0x3a, 0xdb, 0x30, 0x00, 0x06, 0x24, 0x0b, 0x00, 0x00, 0x00, 0xc0};
    std::vector<uint8_t> expectedRes =
        {0x48, 0xfd, 0x00, 0xff, 0x3a, 0xdb, 0xdd, 0x30, 0x00, 0x06, 0x24, 0x0b, 0x00, 0x00, 0x00, 0xdb, 0xdc};
    std::vector<uint8_t> res;
    BinUtils::ApplyByteStuffing(data, rules, std::back_inserter(res));
    ASSERT_EQ(res.size(), expectedRes.size());

    for (size_t i = 0; i < res.size(); ++i) {
        EXPECT_EQ(res[i], expectedRes[i]) << i;
    }
}

TEST(BinUtilsTest, DecodeByteStuffing)
{
    const std::unordered_map<uint8_t, std::vector<uint8_t>> rules = {{0xc0, {0xdb, 0xdc}}, {0xdb, {0xdb, 0xdd}}};
    std::vector<uint8_t> data = {0xc0,
                                 0x48,
                                 0xfd,
                                 0x00,
                                 0xff,
                                 0x3a,
                                 0x57,
                                 0x01,
                                 0x30,
                                 0x00,
                                 0x06,
                                 0x24,
                                 0x0b,
                                 0x00,
                                 0x00,
                                 0x00,
                                 0xdb,
                                 0xdc,
                                 0xc0};
    std::vector<uint8_t> expectedRes =
        {0xc0, 0x48, 0xfd, 0x00, 0xff, 0x3a, 0x57, 0x01, 0x30, 0x00, 0x06, 0x24, 0x0b, 0x00, 0x00, 0x00, 0xc0, 0xc0};
    BinUtils::DecodeByteStuffing(data, rules);
    ASSERT_EQ(data.size(), expectedRes.size());

    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], expectedRes[i]) << i;
    }
}