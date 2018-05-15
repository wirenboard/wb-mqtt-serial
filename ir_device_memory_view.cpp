#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"

#include <cassert>
#include <string.h>


using namespace std;

uint16_t TIRDeviceMemoryBlockView::GetByteIndex(uint16_t index) const
{
    assert(index < MemoryBlock->Size);

    auto byteIndex = (MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian) ? (MemoryBlock->Size - index - 1) : index;

    assert(byteIndex < MemoryBlock->Size);

    return byteIndex;
}

uint16_t TIRDeviceMemoryBlockView::GetValueByteIndex(uint16_t index) const
{
    assert(index < MemoryBlock->Type.GetValueCount());

    uint16_t shift = 0;
    for (uint16_t i = 0; i < index; ++i) {
        shift += RegisterFormatByteWidth(MemoryBlock->Type.Formats[i]);
    }
    return shift;
}

uint16_t TIRDeviceMemoryBlockView::GetValueSize(uint16_t index) const
{
    assert(index < MemoryBlock->Type.GetValueCount());

    return MemoryBlock->Type.IsVariadicSize() ? MemoryBlock->Size : RegisterFormatByteWidth(MemoryBlock->Type.Formats[index]);
}

uint8_t TIRDeviceMemoryBlockView::GetByte(uint16_t index) const
{
    assert(RawMemory);

    return RawMemory[GetByteIndex(index)];
}

void TIRDeviceMemoryBlockView::SetByte(uint16_t index, uint8_t value) const
{
    assert(RawMemory);
    assert(!Readonly);

    RawMemory[GetByteIndex(index)] = value;
}

uint64_t TIRDeviceMemoryBlockView::GetValue(uint16_t index) const
{
    uint64_t value = 0;

    auto start = GetValueByteIndex(index);
    auto end = start + GetValueSize(index);

    for (uint16_t i = start; i < end; ++i) {
        value |= uint64_t(GetByte(i)) << ((i - start) * 8);
    }

    return value;
}

void TIRDeviceMemoryBlockView::SetValue(uint16_t index, uint64_t value) const
{
    auto start = GetValueByteIndex(index);
    auto end = start + GetValueSize(index);

    for (uint16_t i = start; i < end; ++i) {
        SetByte(i, value >> ((i - start) * 8));
    }
}

TIRDeviceMemoryView::TIRDeviceMemoryView(uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t start, uint16_t blockSize, bool readonly)
    : RawMemory(memory)
    , Size(size)
    , Type(type)
    , StartAddress(start)
    , BlockSize(blockSize)
    , Readonly(readonly)
{}

TIRDeviceMemoryView::TIRDeviceMemoryView(const uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t start, uint16_t blockSize)
    : TIRDeviceMemoryView(
        const_cast<uint8_t *>(memory),
        size,
        type,
        start,
        blockSize,
        true
    )
{}

void TIRDeviceMemoryView::Clear() const
{
    assert(RawMemory);
    assert(!Readonly);

    memset(RawMemory, 0, Size);
}

uint16_t TIRDeviceMemoryView::GetBlockStart(const CPMemoryBlock & memoryBlock) const
{
    assert(&memoryBlock->Type == &Type);
    assert(memoryBlock->Address >= StartAddress);

    auto byteIndex = (memoryBlock->Address - StartAddress) * BlockSize;

    assert(byteIndex < Size);

    return byteIndex;
}
