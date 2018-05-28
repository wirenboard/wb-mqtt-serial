#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"
#include "memory_block_bind_info.h"

#include <cassert>
#include <iostream>
#include <bitset>
#include <string.h>


using namespace std;


void TIRDeviceMemoryBlockValue::GetValueImpl(uint8_t * pValue, uint8_t size) const
{
    assert(size >= (EndByteIndex - StartByteIndex)); // make sure all memory is used

    for (auto i = StartByteIndex; i < EndByteIndex; ++i) {
        // we need to get byte by view, because we define byte order by view
        (*pValue++) = View.GetByte(IsLittleEndian() ? i : (i - StartByteIndex));
    }
}

void TIRDeviceMemoryBlockValue::SetValueImpl(const uint8_t * pValue, uint8_t size)
{
    assert(size >= (EndByteIndex - StartByteIndex)); // make sure all memory is used

    for (auto i = StartByteIndex; i < EndByteIndex; ++i) {
        // we need to set byte by view, because we define byte order by view
        View.SetByte(IsLittleEndian() ? i : (i - StartByteIndex), (*pValue++));
    }
}

TIRDeviceMemoryBlockMemory & TIRDeviceMemoryBlockMemory::operator=(const TIRDeviceMemoryBlockMemory & rhs)
{
    assert(*View.MemoryBlock == *rhs.View.MemoryBlock);

    if (View.RawMemory && rhs.View.RawMemory) {
        memcpy(View.RawMemory, rhs.View.RawMemory, View.MemoryBlock->Size);
    }

    return *this;
}

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

TIRDeviceMemoryBlockView TIRDeviceMemoryView::operator[](const CPMemoryBlock & memoryBlock) const
{
    assert(&memoryBlock->Type == &Type);
    assert(memoryBlock->Address >= StartAddress);

    auto byteIndex = (memoryBlock->Address - StartAddress) * BlockSize;

    assert(byteIndex < Size);

    return { RawMemory + byteIndex, memoryBlock, Readonly };
}

uint64_t TIRDeviceMemoryView::ReadValue(const TIRDeviceValueDesc & valueDesc) const
{
    auto & self = *this;
    uint64_t value = 0;

    auto readMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & boundMemoryBlock) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        const auto mask = bindInfo.GetMask();
        const auto & memoryBlockView = self[memoryBlock];

        for (int iByte = (bindInfo.BitEnd - 1) / 8; iByte >= int(bindInfo.BitStart / 8); --iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                break;

            value <<= (end - begin);
            value |= ((mask >> (iByte * 8)) & memoryBlockView.GetByte(iByte)) >> begin;
        }

        // TODO: check for usefullness
        if (Global::Debug) {
            std::cerr << "mb mask: " << std::bitset<64>(mask) << std::endl;
            // std::cerr << "reading " << bindInfo.Describe() << " bits of " << memoryView.MemoryBlock->Describe()
            //         << " to [" << (int)offset << ", " << int(offset + bindInfo.BitCount() - 1) << "] bits of value" << std::endl;
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        std::for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), readMemoryBlock);
    } else {
        std::for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), readMemoryBlock);
    }

    if (Global::Debug)
        std::cerr << "map value from registers: " << value << std::endl;

    return value;
}

void TIRDeviceMemoryView::WriteValue(const TIRDeviceValueDesc & valueDesc, uint64_t value) const
{
    WriteValue(valueDesc, value, [this](const CPMemoryBlock & mb) {
        return (*this)[mb];
    });
}

void TIRDeviceMemoryView::WriteValue(const TIRDeviceValueDesc & valueDesc, uint64_t value, TMemoryBlockViewProvider getView)
{
    if (Global::Debug)
        std::cerr << "map value to registers: " << value << std::endl;

    auto writeMemoryBlock = [&](const std::pair<const PMemoryBlock, TMemoryBlockBindInfo> & memoryBlockBindInfo){
        const auto & memoryBlock = memoryBlockBindInfo.first;
        const auto & bindInfo = memoryBlockBindInfo.second;

        const auto & memoryBlockView = getView(memoryBlock);

        if (!memoryBlockView) {
            value >>= bindInfo.BitCount();
            return;
        }

        const auto mask = bindInfo.GetMask();
        const auto & cache = memoryBlock->GetCache();

        for (uint16_t iByte = bindInfo.BitStart / 8; iByte < memoryBlock->Size; ++iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                break;

            const auto byteMask = mask >> (iByte * 8);
            const auto cachedByte = cache ? cache.GetByte(iByte) : uint8_t(0);

            memoryBlockView.SetByte(iByte, (~byteMask & cachedByte) | (byteMask & (value << begin)));
            value >>= (end - begin);
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), writeMemoryBlock);
    } else {
        for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), writeMemoryBlock);
    }
}
