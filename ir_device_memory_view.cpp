#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"
#include "ir_bind_info.h"

#include <cassert>
#include <iostream>
#include <bitset>
#include <string.h>


using namespace std;


TIRDeviceMemoryBlockValueBase::TIRDeviceMemoryBlockValueBase(uint8_t * raw, uint8_t size, bool readonly, bool isLE)
    : RawMemory(raw)
    , Size(size)
    , Readonly(readonly)
    , IsLE(isLE)
{
    assert(Size <= 8 && "fundamental value cannot be more than 8 bytes (64 bits)");
}

void TIRDeviceMemoryBlockValueBase::GetValueImpl(uint8_t * pValue) const
{
    for (uint8_t iByte = 0; iByte < Size; ++iByte) {
        // we need to get byte by view, because we define byte order by view
        (*pValue++) = GetByte(iByte);
    }
}

void TIRDeviceMemoryBlockValueBase::SetValueImpl(const uint8_t * pValue) const
{
    for (uint8_t iByte = 0; iByte < Size; ++iByte) {
        // we need to set byte by view, because we define byte order by view
        SetByte(iByte, (*pValue++));
    }
}

uint8_t TIRDeviceMemoryBlockValueBase::PlatformEndiannesAware(uint8_t iByte) const
{
    return IsLittleEndian() ? iByte : Size - iByte - 1;
}

uint8_t TIRDeviceMemoryBlockValueBase::MemoryBlockEndiannesAware(uint8_t iByte) const
{
    return IsLE ? iByte : Size - iByte - 1;
}

uint8_t TIRDeviceMemoryBlockValueBase::GetByte(uint8_t iByte) const
{
    assert(RawMemory);

    return RawMemory[MemoryBlockEndiannesAware(PlatformEndiannesAware(iByte))];
}

void TIRDeviceMemoryBlockValueBase::SetByte(uint8_t iByte, uint8_t value) const
{
    assert(RawMemory);
    assert(!Readonly);

    RawMemory[MemoryBlockEndiannesAware(PlatformEndiannesAware(iByte))] = value;
}

TIRDeviceMemoryBlockMemory & TIRDeviceMemoryBlockMemory::operator=(const TIRDeviceMemoryBlockMemory & rhs)
{
    assert(*View.MemoryBlock == *rhs.View.MemoryBlock);

    if (View.RawMemory && rhs.View.RawMemory) {
        memcpy(View.RawMemory, rhs.View.RawMemory, View.MemoryBlock->Size);
    }

    return *this;
}

TIRDeviceMemoryBlockValue TIRDeviceMemoryBlockViewBase::operator[](uint16_t index) const
{
    // auto valueCount = MemoryBlock->Type.GetValueCount();
    // assert(index < valueCount);
    // index = MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian ? index : valueCount - index - 1;

    return {
        RawMemory + MemoryBlock->Type.GetValueByteIndex(index),
        GetValueSize(index),
        Readonly,
        MemoryBlock->Type.ByteOrder == EByteOrder::LittleEndian
    };
}

uint16_t TIRDeviceMemoryBlockViewBase::GetByteIndex(uint16_t index) const
{
    CheckByteIndex(index);
    auto byteIndex = (MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian) ? (MemoryBlock->Size - index - 1) : index;
    CheckByteIndex(byteIndex);
    return byteIndex;
}

uint8_t TIRDeviceMemoryBlockViewBase::GetValueSize(uint16_t index) const
{
    if (MemoryBlock->Type.IsVariadicSize()) {
        assert(MemoryBlock->Size <= 8);
        return static_cast<uint8_t>(MemoryBlock->Size);
    } else {
        return MemoryBlock->Type.GetValueSize(index);
    }
}

uint16_t TIRDeviceMemoryBlockViewBase::GetSize() const
{
    return MemoryBlock->Size;
}

uint8_t TIRDeviceMemoryBlockViewBase::GetByte(uint16_t index) const
{
    assert(RawMemory);

    return RawMemory[GetByteIndex(index)];
}

void TIRDeviceMemoryBlockViewBase::SetByte(uint16_t index, uint8_t value) const
{
    assert(RawMemory);
    assert(!Readonly);

    RawMemory[GetByteIndex(index)] = value;
}

void TIRDeviceMemoryBlockViewBase::CheckByteIndex(uint16_t index) const
{
    assert(index < GetSize());
}

void TIRDeviceMemoryBlockViewBase::GetValueImpl(uint8_t * pValue) const
{
    auto valueCount = MemoryBlock->Type.GetValueCount();
    for (uint16_t iValue = 0; iValue < valueCount; ++iValue) {
        const auto & valueView = (*this)[iValue];
        valueView.GetValueImpl(pValue);
        pValue += valueView.Size;
    }
}

void TIRDeviceMemoryBlockViewBase::SetValueImpl(const uint8_t * pValue) const
{
    auto valueCount = MemoryBlock->Type.GetValueCount();
    for (uint16_t iValue = 0; iValue < valueCount; ++iValue) {
        const auto & valueView = (*this)[iValue];
        valueView.SetValueImpl(pValue);
        pValue += valueView.Size;
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

    auto readMemoryBlock = [&](const std::pair<const PMemoryBlock, TIRBindInfo> & boundMemoryBlock) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        const auto mask = bindInfo.GetMask();
        const auto & memoryBlockView = self[memoryBlock];

        for (int iByte = (bindInfo.BitEnd - 1) / 8; iByte >= int(bindInfo.BitStart / 8); --iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                break;

            auto shift = bindInfo.BitStart - iByte * 8;
            uint8_t byteMask = shift > 0 ? (mask << shift) : (mask >> -shift);

            value <<= (end - begin);
            value |= (byteMask & memoryBlockView.GetByte(iByte)) >> begin;
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

    auto writeMemoryBlock = [&](const std::pair<const PMemoryBlock, TIRBindInfo> & memoryBlockBindInfo){
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

            auto shift = bindInfo.BitStart - iByte * 8;
            uint8_t byteMask = shift > 0 ? (mask << shift) : (mask >> -shift);

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
