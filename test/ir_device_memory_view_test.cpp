#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"


#include <gtest/gtest.h>


using namespace std;

class TIRDeviceMemoryViewTest: public ::testing::Test
{};

class TIRDeviceMemoryBlockValueTest: public ::testing::Test
{
protected:
    TMemoryBlockType TypeFloatBE {0, "float", "value", { Float }};
    TMemoryBlockType TypeU32BE   {0, "u32", "value", { U32 }};

    TMemoryBlockType TypeFloatLE {0, "float", "value", { Float }, false, EByteOrder::LittleEndian};
    TMemoryBlockType TypeU32LE   {0, "u32", "value", { U32 }, false, EByteOrder::LittleEndian};
};

TEST_F(TIRDeviceMemoryBlockValueTest, IntegralValueBE)
{
    uint8_t raw[4] { 0x01, 0xc0, 0x60, 0x34 };      // 29384756 in big-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32BE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(29384756u, memoryView[0]);
}

TEST_F(TIRDeviceMemoryBlockValueTest, FloatingPointValueBE)
{
    uint8_t raw[4] { 0x40, 0x9b, 0xd7, 0x0a };      // 4.87 in big-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeFloatBE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(4.87f, memoryView[0]);
}


TEST_F(TIRDeviceMemoryBlockValueTest, IntegralValueLE)
{
    uint8_t raw[4] { 0x34, 0x60, 0xc0, 0x01 };      // 29384756 in little-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeU32LE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(29384756u, memoryView[0]);
}

TEST_F(TIRDeviceMemoryBlockValueTest, FloatingPointValueLE)
{
    uint8_t raw[4] { 0x0a, 0xd7, 0x9b, 0x40 };      // 4.87 in little-endian

    auto memoryBlock = make_shared<TMemoryBlock>(0, TypeFloatLE);
    TIRDeviceMemoryBlockView memoryView { raw, memoryBlock, true };

    EXPECT_EQ(4.87f, memoryView[0]);
}
