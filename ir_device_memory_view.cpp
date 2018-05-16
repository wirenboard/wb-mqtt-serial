#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"
#include "memory_block_bind_info.h"

#include <cassert>
#include <iostream>
#include <bitset>
#include <string.h>


using namespace std;

namespace
{
    inline void ReadFromMemory(const TIRDeviceMemoryBlockView & memoryView, const TMemoryBlockBindInfo & bindInfo, uint8_t offset, uint64_t & value)
    {
        const auto mask = bindInfo.GetMask();

        for (uint16_t iByte = bindInfo.BitStart / 8; iByte < memoryView.MemoryBlock->Size; ++iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                continue;

            uint64_t bits = (mask >> (iByte * 8)) & (memoryView.GetByte(iByte) >> begin);
            value |= bits << offset;

            auto bitCount = end - begin;
            offset += bitCount;
        }

        // TODO: check for usefullness
        if (Global::Debug) {
            std::cerr << "mb mask: " << std::bitset<64>(mask) << std::endl;
            std::cerr << "reading " << bindInfo.Describe() << " bits of " << memoryView.MemoryBlock->Describe()
                    << " to [" << (int)offset << ", " << int(offset + bindInfo.BitCount() - 1) << "] bits of value" << std::endl;
        }
    }



    inline void WriteToMemory(const TIRDeviceMemoryBlockView & memoryView, const TMemoryBlockBindInfo & bindInfo, uint8_t offset, const uint64_t & value)
    {
        const auto mask = bindInfo.GetMask();

        for (uint16_t iByte = bindInfo.BitStart / 8; iByte < memoryView.MemoryBlock->Size; ++iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                continue;

            uint8_t byteMask = mask >> (iByte * 8);

            memoryView.SetByte(iByte, byteMask & ((value >> offset) << begin));

            auto bitCount = end - begin;
            offset += bitCount;
        }
    }
}

uint16_t TIRDeviceMemoryBlockView::GetByteIndex(uint16_t index) const
{
    assert(index < MemoryBlock->Size);

    auto byteIndex = (MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian) ? index : (MemoryBlock->Size - index - 1);

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

uint64_t TIRDeviceMemoryView::ReadValue(const TIRDeviceValueDesc & valueDesc) const
{
    auto & self = *this;
    uint64_t value = 0;
    uint8_t bitPosition = 0;

    auto readMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & boundMemoryBlock) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        ReadFromMemory(self[memoryBlock], bindInfo, bitPosition, value);
        bitPosition += bindInfo.BitCount();
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        std::for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), readMemoryBlock);
    } else {
        std::for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), readMemoryBlock);
    }

    if (Global::Debug)
        std::cerr << "map value from registers: " << value << std::endl;

    return value;
}

void TIRDeviceMemoryView::WriteValue(const TIRDeviceValueDesc & valueDesc, uint64_t value) const
{
    if (Global::Debug)
        std::cerr << "map value to registers: " << value << std::endl;

    auto & self = *this;
    uint8_t bitPosition = 0;

    auto writeMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & memoryBlockBindInfo){
        const auto & memoryBlock = memoryBlockBindInfo.first;
        const auto & bindInfo = memoryBlockBindInfo.second;

        const auto & memoryBlockView = self[memoryBlock];

        WriteToMemory(memoryBlockView, bindInfo, bitPosition, value);
        bitPosition += bindInfo.BitCount();

        // apply cache if memory block is cached
        if (const auto & cache = memoryBlock->GetCache()) {
            auto mask = bindInfo.GetMask();

            for (uint16_t iByte = 0; iByte < memoryBlock->Size; ++iByte) {
                memoryBlockView.SetByte(iByte, (~mask & cache[iByte]) | (mask & memoryBlockView.GetByte(iByte)));
                mask >>= 8;
            }
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), writeMemoryBlock);
    } else {
        for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), writeMemoryBlock);
    }
}
