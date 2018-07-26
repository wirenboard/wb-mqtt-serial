#include "ir_device_memory_view.h"
#include "ir_value.h"
#include "ir_bind_info.h"
#include "memory_block.h"
#include "register_config.h"
#include "virtual_value.h"


#include <gtest/gtest.h>

using namespace std;

class TIRDeviceMemoryBlockValueTest: public ::testing::Test
{
protected:
    TMemoryBlockType TypeFloatBE   {0, "float", "value", { Float }};
    TMemoryBlockType TypeU32BE     {0, "u32", "value", { U32 }};
    TMemoryBlockType TypeComplexBE {0, "complex", "value", { U8, U32, Float, U16, Double }};

    TMemoryBlockType TypeFloatLE   {0, "float", "value", { Float }, false, EByteOrder::LittleEndian};
    TMemoryBlockType TypeU32LE     {0, "u32", "value", { U32 }, false, EByteOrder::LittleEndian};
    TMemoryBlockType TypeComplexLE {0, "complex", "value", { U8, U32, Float, U16, Double }, false, EByteOrder::LittleEndian};
};

TEST_F(TIRDeviceMemoryBlockValueTest, IntegralValueBE)
{
    uint8_t raw[4] { 0x01, 0xc0, 0x60, 0x34 };      // 29384756 in big-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32BE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(29384756u, (uint32_t)memoryView[0]);
}

TEST_F(TIRDeviceMemoryBlockValueTest, FloatingPointValueBE)
{
    uint8_t raw[4] { 0x40, 0x9b, 0xd7, 0x0a };      // 4.87 in big-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeFloatBE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(4.87f, (float)memoryView[0]);
}

TEST_F(TIRDeviceMemoryBlockValueTest, UserTypeBE)
{
    struct __attribute__ ((packed)) TUserType
    {
        uint8_t     v1; // size: 1
        uint32_t    v2; // size: 4
        float       v3; // size: 4
        uint16_t    v4; // size: 2
        double      v5; // size: 8
    };                  // size: 19

    uint8_t raw[sizeof(TUserType)] {
        0x0a,                   // v1 = 0x0a
        0xfc, 0xce, 0xb0, 0x22, // v2 = 4241403938
        0x40, 0x9b, 0xd7, 0x0a, // v3 = 4.87
        0xa9, 0x9d,             // v4 = 43421
        0x3f, 0xf3, 0xc0, 0xca, 0x42, 0x8a, 0xa7, 0x9b  // v5 = 1.234567890098765432
    };

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeComplexBE);
    TUserType val;
    {
        TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

        val = memoryView;

        EXPECT_EQ(0x0a, val.v1);
        EXPECT_EQ(4241403938, val.v2);
        EXPECT_EQ(4.87f, val.v3);
        EXPECT_EQ(43421, val.v4);
        EXPECT_EQ(1.234567890098765432, val.v5);

        EXPECT_EQ(val.v1, (uint8_t)memoryView[0]);
        EXPECT_EQ(val.v2, (uint32_t)memoryView[1]);
        EXPECT_EQ(val.v3, (float)memoryView[2]);
        EXPECT_EQ(val.v4, (uint16_t)memoryView[3]);
        EXPECT_EQ(val.v5, (double)memoryView[4]);
    }

    uint8_t iraw[sizeof(TUserType)] = {0};

    {
        TIRDeviceMemoryBlockView memoryView { iraw, memoryBlock, false };
        memoryView[0] = val.v1;
        memoryView[1] = val.v2;
        memoryView[2] = val.v3;
        memoryView[3] = val.v4;
        memoryView[4] = val.v5;

        EXPECT_EQ(val.v1, (uint8_t)memoryView[0]);
        EXPECT_EQ(val.v2, (uint32_t)memoryView[1]);
        EXPECT_EQ(val.v3, (float)memoryView[2]);
        EXPECT_EQ(val.v4, (uint16_t)memoryView[3]);
        EXPECT_EQ(val.v5, (double)memoryView[4]);

        EXPECT_EQ(0, memcmp(iraw, raw, sizeof(iraw)));
    }

    memset(iraw, 0, sizeof iraw);

    {
        TIRDeviceMemoryBlockView memoryView { iraw, memoryBlock, false };
        memoryView = val;

        EXPECT_EQ(val.v1, (uint8_t)memoryView[0]);
        EXPECT_EQ(val.v2, (uint32_t)memoryView[1]);
        EXPECT_EQ(val.v3, (float)memoryView[2]);
        EXPECT_EQ(val.v4, (uint16_t)memoryView[3]);
        EXPECT_EQ(val.v5, (double)memoryView[4]);
    }

    EXPECT_EQ(0, memcmp(iraw, raw, sizeof(iraw)));
}


TEST_F(TIRDeviceMemoryBlockValueTest, IntegralValueLE)
{
    uint8_t raw[4] { 0x34, 0x60, 0xc0, 0x01 };      // 29384756 in little-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32LE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(29384756u, (uint32_t)memoryView[0]);
}

TEST_F(TIRDeviceMemoryBlockValueTest, FloatingPointValueLE)
{
    uint8_t raw[4] { 0x0a, 0xd7, 0x9b, 0x40 };      // 4.87 in little-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeFloatLE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(4.87f, (float)memoryView[0]);
}


TEST_F(TIRDeviceMemoryBlockValueTest, UserTypeLE)
{
    struct __attribute__ ((packed)) TUserType
    {
        uint8_t     v1; // size: 1
        uint32_t    v2; // size: 4
        float       v3; // size: 4
        uint16_t    v4; // size: 2
        double      v5; // size: 8
    };                  // size: 19

    uint8_t raw[sizeof(TUserType)] {
        0x0a,                   // v1 = 0x0a
        0xfc, 0xce, 0xb0, 0x22, // v2 = 4241403938
        0x40, 0x9b, 0xd7, 0x0a, // v3 = 4.87
        0xa9, 0x9d,             // v4 = 43421
        0x3f, 0xf3, 0xc0, 0xca, 0x42, 0x8a, 0xa7, 0x9b  // v5 = 1.234567890098765432
    };

    std::reverse(raw, raw + sizeof(raw));

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeComplexLE);
    TUserType val;
    {
        TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

        val = memoryView;

        EXPECT_EQ(0x0a, val.v1);
        EXPECT_EQ(4241403938, val.v2);
        EXPECT_EQ(4.87f, val.v3);
        EXPECT_EQ(43421, val.v4);
        EXPECT_EQ(1.234567890098765432, val.v5);

        EXPECT_EQ(val.v1, (uint8_t)memoryView[0]);
        EXPECT_EQ(val.v2, (uint32_t)memoryView[1]);
        EXPECT_EQ(val.v3, (float)memoryView[2]);
        EXPECT_EQ(val.v4, (uint16_t)memoryView[3]);
        EXPECT_EQ(val.v5, (double)memoryView[4]);
    }

    uint8_t iraw[sizeof(TUserType)] = {0};

    {
        TIRDeviceMemoryBlockView memoryView { iraw, memoryBlock, false };
        memoryView[0] = val.v1;
        memoryView[1] = val.v2;
        memoryView[2] = val.v3;
        memoryView[3] = val.v4;
        memoryView[4] = val.v5;

        EXPECT_EQ(val.v1, (uint8_t)memoryView[0]);
        EXPECT_EQ(val.v2, (uint32_t)memoryView[1]);
        EXPECT_EQ(val.v3, (float)memoryView[2]);
        EXPECT_EQ(val.v4, (uint16_t)memoryView[3]);
        EXPECT_EQ(val.v5, (double)memoryView[4]);
    }

    EXPECT_EQ(0, memcmp(iraw, raw, sizeof(iraw)));
}


class TIRDeviceMemoryViewTest: public TIRDeviceMemoryBlockValueTest
{};

TEST_F(TIRDeviceMemoryViewTest, ReadSimple)
{
    uint8_t raw[]{
        0x49, 0x96, 0x02, 0xd2, // 1234567890 addr 0
        0x8b, 0xd0, 0x38, 0x35, // 2345678901 addr 1
        0xce, 0x0a, 0x6a, 0x14, // 3456789012 addr 2
        0xfe, 0x62, 0xd5, 0xcb, // 4267890123 addr 3
    };

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);
    auto memoryBlock2 = make_shared<TMemoryBlock>(2, TypeU32BE);
    auto memoryBlock3 = make_shared<TMemoryBlock>(3, TypeU32BE);

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("1234567890", value->GetTextValue(*regConfig));

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock1, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("2345678901", value->GetTextValue(*regConfig));

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock2, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("3456789012", value->GetTextValue(*regConfig));

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock3, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("4267890123", value->GetTextValue(*regConfig));
}

TEST_F(TIRDeviceMemoryViewTest, ReadSingleShiftedPartialByte)
{
    uint8_t raw[]{
        0x01, 0xbb, 0xe5, 0x00 // 454548 << 6 addr 0
    };

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32BE);

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock, {6, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("454548", value->GetTextValue(*regConfig));
}

TEST_F(TIRDeviceMemoryViewTest, ReadMultiShifted)
{
    uint8_t raw[]{
        0x00, 0x00, 0x03, 0xf9, // 4267890123 << 10 addr 0, 1
        0x8b, 0x57, 0x2c, 0x00,
    };

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 10} /*TIRBindInfo*/},
                {memoryBlock1, {10, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("4267890123", value->GetTextValue(*regConfig));
}

TEST_F(TIRDeviceMemoryViewTest, ReadMultiShiftedPartialByte)
{
    uint8_t raw[]{
        0x00, 0x00, 0x00, 0x0c, // 12890123 << 12 addr 0, 1
        0x4b, 0x00, 0xb0, 0x00,
    };

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);

    memView.ReadValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 5} /*TIRBindInfo*/},
                {memoryBlock1, {12, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ("12890123", value->GetTextValue(*regConfig));
}

TEST_F(TIRDeviceMemoryViewTest, WriteSimple)
{
    const uint8_t expectedRaw[]{
        0x49, 0x96, 0x02, 0xd2, // 1234567890 addr 0
        0x8b, 0xd0, 0x38, 0x35, // 2345678901 addr 1
        0xce, 0x0a, 0x6a, 0x14, // 3456789012 addr 2
        0xfe, 0x62, 0xd5, 0xcb, // 4267890123 addr 3
    };

    uint8_t raw[sizeof expectedRaw] {0};

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);
    auto memoryBlock2 = make_shared<TMemoryBlock>(2, TypeU32BE);
    auto memoryBlock3 = make_shared<TMemoryBlock>(3, TypeU32BE);

    value->SetTextValue(*regConfig, "1234567890");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    value->SetTextValue(*regConfig, "2345678901");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock1, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    value->SetTextValue(*regConfig, "3456789012");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock2, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    value->SetTextValue(*regConfig, "4267890123");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock3, {0, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ(0, memcmp(raw, expectedRaw, sizeof(raw)));
}

TEST_F(TIRDeviceMemoryViewTest, WriteSingleShiftedPartialByte)
{
    const uint8_t expectedRaw[]{
        0x01, 0xbb, 0xe5, 0x00 // 454548 << 6 addr 0
    };

    uint8_t raw[sizeof expectedRaw] {0};

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32BE);

    value->SetTextValue(*regConfig, "454548");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock, {6, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ(0, memcmp(raw, expectedRaw, sizeof(raw)));
}

TEST_F(TIRDeviceMemoryViewTest, WriteMultiShifted)
{
    const uint8_t expectedRaw[]{
        0x00, 0x00, 0x03, 0xf9, // 4267890123 << 10 addr 0, 1
        0x8b, 0x57, 0x2c, 0x00,
    };

    uint8_t raw[sizeof expectedRaw] {0};

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);

    value->SetTextValue(*regConfig, "4267890123");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 10} /*TIRBindInfo*/},
                {memoryBlock1, {10, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ(0, memcmp(raw, expectedRaw, sizeof(raw)));
}

TEST_F(TIRDeviceMemoryViewTest, WriteMultiShiftedPartialByte)
{
    const uint8_t expectedRaw[]{
        0x00, 0x00, 0x00, 0x0c, // 12890123 << 12 addr 0, 1
        0x4b, 0x00, 0xb0, 0x00,
    };

    uint8_t raw[sizeof expectedRaw] {0};

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);

    value->SetTextValue(*regConfig, "12890123");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        {
            { /*TBoundMemoryBlocks*/
                {memoryBlock0, {0, 5} /*TIRBindInfo*/},
                {memoryBlock1, {12, 32} /*TIRBindInfo*/}
            },
            EWordOrder::BigEndian
        },
        *value
    });

    EXPECT_EQ(0, memcmp(raw, expectedRaw, sizeof(raw)));
}

// unsupported case: write many overlapping (common byte) ir values -
// not needed because we write only one ir value before flush
#if 0
TEST_F(TIRDeviceMemoryViewTest, WriteMultiShiftedPartialByteMany)
{
    const uint8_t expectedRaw[]{
        0x00, 0xdf, 0x18, 0xec, // 456903 << 5 addr 0, 12890123 << 12 addr 0, 1
        0x4b, 0x00, 0xb0, 0x00,
    };

    uint8_t raw[sizeof expectedRaw] {0};

    auto regConfig = TRegisterConfig::Create(0, 0, U32);
    auto value = TIRValue::Make(*regConfig);

    TIRDeviceMemoryView memView{raw, sizeof raw, TypeU32BE, 0, 4};

    auto memoryBlock0 = make_shared<TMemoryBlock>(0, TypeU32BE);
    auto memoryBlock1 = make_shared<TMemoryBlock>(1, TypeU32BE);

    value->SetTextValue(*regConfig, "12890123");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        { /*TBoundMemoryBlocks*/
            {memoryBlock0, {0, 5} /*TIRBindInfo*/},
            {memoryBlock1, {12, 32} /*TIRBindInfo*/}
        },
        EWordOrder::BigEndian,
        *value
    });

    value->SetTextValue(*regConfig, "456903");

    memView.WriteValue({ /*TIRDeviceValueContext*/
        { /*TBoundMemoryBlocks*/
            {memoryBlock0, {5, 32} /*TIRBindInfo*/}
        },
        EWordOrder::BigEndian,
        *value
    });

    EXPECT_EQ(0, memcmp(raw, expectedRaw, sizeof(raw)));
}
#endif
