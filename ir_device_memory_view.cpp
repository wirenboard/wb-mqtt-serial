#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"
#include "ir_bind_info.h"
#include "ir_value_formatter.h"

#include <cassert>
#include <iostream>
#include <bitset>
#include <string.h>


using namespace std;


TIRDeviceValueViewImpl::TIRDeviceValueViewImpl(uint8_t * raw, uint8_t size, bool readonly, bool isLE)
    : RawMemory(raw)
    , Size(size)
    , Readonly(readonly)
    , IsLE(isLE)
{
    assert(Size <= 8 && "fundamental value cannot be more than 8 bytes (64 bits)");
}

void TIRDeviceValueViewImpl::GetValueImpl(uint8_t * pValue) const
{
    for (uint8_t iByte = 0; iByte < Size; ++iByte) {
        // we need to get byte by view, because we define byte order by view
        (*pValue++) = GetByte(iByte);
    }
}

void TIRDeviceValueViewImpl::SetValueImpl(const uint8_t * pValue) const
{
    for (uint8_t iByte = 0; iByte < Size; ++iByte) {
        // we need to set byte by view, because we define byte order by view
        SetByte(iByte, (*pValue++));
    }
}

uint8_t TIRDeviceValueViewImpl::PlatformEndiannesAware(uint8_t iByte) const
{
    return IsLittleEndian() ? iByte : Size - iByte - 1;
}

uint8_t TIRDeviceValueViewImpl::MemoryBlockEndiannesAware(uint8_t iByte) const
{
    return IsLE ? iByte : Size - iByte - 1;
}

uint8_t TIRDeviceValueViewImpl::GetByte(uint8_t iByte) const
{
    assert(RawMemory);

    return RawMemory[MemoryBlockEndiannesAware(PlatformEndiannesAware(iByte))];
}

void TIRDeviceValueViewImpl::SetByte(uint8_t iByte, uint8_t value) const
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

TIRDeviceValueView TIRDeviceMemoryBlockViewImpl::operator[](uint16_t index) const
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

uint16_t TIRDeviceMemoryBlockViewImpl::GetByteIndex(uint16_t index) const
{
    CheckByteIndex(index);
    auto byteIndex = (MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian) ? (MemoryBlock->Size - index - 1) : index;
    CheckByteIndex(byteIndex);
    return byteIndex;
}

uint8_t TIRDeviceMemoryBlockViewImpl::GetValueSize(uint16_t index) const
{
    if (MemoryBlock->Type.IsVariadicSize()) {
        assert(MemoryBlock->Size <= 8);
        return static_cast<uint8_t>(MemoryBlock->Size);
    } else {
        return MemoryBlock->Type.GetValueSize(index);
    }
}

uint16_t TIRDeviceMemoryBlockViewImpl::GetSize() const
{
    return MemoryBlock->Size;
}

uint8_t TIRDeviceMemoryBlockViewImpl::GetByte(uint16_t index) const
{
    assert(RawMemory);

    return RawMemory[GetByteIndex(index)];
}

void TIRDeviceMemoryBlockViewImpl::SetByte(uint16_t index, uint8_t value) const
{
    assert(RawMemory);
    assert(!Readonly);

    RawMemory[GetByteIndex(index)] = value;
}

void TIRDeviceMemoryBlockViewImpl::CheckByteIndex(uint16_t index) const
{
    assert(index < GetSize());
}

void TIRDeviceMemoryBlockViewImpl::GetValueImpl(uint8_t * pValue) const
{
    auto valueCount = MemoryBlock->Type.GetValueCount();
    for (uint16_t iValue = 0; iValue < valueCount; ++iValue) {
        const auto & valueView = (*this)[iValue];
        valueView.GetValueImpl(pValue);
        pValue += valueView.Size;
    }
}

void TIRDeviceMemoryBlockViewImpl::SetValueImpl(const uint8_t * pValue) const
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

void TIRDeviceMemoryView::ReadValue(const TIRDeviceValueContext & context) const
{
    auto & self = *this;

    uint8_t byte {0};
    uint8_t bitsNeeded {0};
    bool first = true;

    auto byte_is_ready = [&]{
        return bitsNeeded == 0;
    };

    auto push_bits = [&](uint8_t bits, uint8_t count) {
        assert(!byte_is_ready());
        auto bitsToTake = min(bitsNeeded, count);
        byte <<= bitsToTake;
        byte |= bits >> (count - bitsToTake);
        bitsNeeded -= bitsToTake;
        return bitsToTake;
    };

    auto get_byte = [&]{
        assert(byte_is_ready());
        bitsNeeded = 8;
        return byte;
    };

    TBitBuffer bitBuffer;

    auto readMemoryBlock = [&](const std::pair<const PMemoryBlock, TIRBindInfo> & boundMemoryBlock) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        const auto mask = bindInfo.GetMask();
        const auto & memoryBlockView = self[memoryBlock];

        if (first) {
            auto cnt = bindInfo.BitCount();
            bitBuffer.Capacity = cnt % 8 ? cnt - (cnt / 8) * 8 : 8;
            first = false;
        }

        // iByte is always little-endian (lower value signifies lower byte)
        for (int iByte = (bindInfo.BitEnd - 1) / 8; iByte >= int(bindInfo.BitStart / 8); --iByte) {
            auto begin = std::max(0, bindInfo.BitStart - iByte * 8);
            auto end = std::min(8, bindInfo.BitEnd - iByte * 8);

            if (begin >= end)
                break;

            auto shift = bindInfo.BitStart - iByte * 8;
            uint8_t byteMask = shift > 0 ? (mask << shift) : (mask >> -shift);

            auto count = end - begin;

            TBitBuffer in{
                (byteMask & memoryBlockView.GetByte(iByte)) >> begin,
                count,
                count
            };

            bitBuffer << in;
            if (bitBuffer.IsFull()) {
                context.Value << bitBuffer;
                bitBuffer.Reset();
            }

            if (in) {
                bitBuffer << in;
            }

            assert(!in);
        }
    };

    if (context.WordOrder == EWordOrder::BigEndian) {
        ForEach(context.BoundMemoryBlocks, readMemoryBlock);
    } else {
        ForEachReverse(context.BoundMemoryBlocks, readMemoryBlock);
    }

    assert(!bitBuffer);
}

void TIRDeviceMemoryView::WriteValue(const TIRDeviceValueContext & context) const
{
    WriteValue(context, [this](const CPMemoryBlock & mb) {
        return (*this)[mb];
    });
}

void TIRDeviceMemoryView::WriteValue(const TIRDeviceValueContext & context, TMemoryBlockViewProvider getView)
{
    uint8_t bitsToSkip = 0;
    TBitBuffer bitBuffer;

    auto writeMemoryBlock = [&](const std::pair<const PMemoryBlock, TIRBindInfo> & memoryBlockBindInfo){
        const auto & memoryBlock = memoryBlockBindInfo.first;
        const auto & bindInfo = memoryBlockBindInfo.second;

        const auto & memoryBlockView = getView(memoryBlock);

        if (!memoryBlockView) {
            bitsToSkip += bindInfo.BitCount();
            auto bytesToSkip = bitsToSkip / 8;
            bitsToSkip -= bytesToSkip * 8;
            context.Value.SkipBytes(bytesToSkip);
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

            TBitBuffer out { 0, end - begin, end - begin };
            TBitBuffer in { 0, 8 - bitsToSkip, 8 - bitsToSkip };

            out << bitBuffer;

            while(!out.IsFull()) {
                context.Value >> in.Buffer;
                in.Buffer >>= bitsToSkip;
                bitsToSkip = 0;
                out << in;
            }

            bitBuffer << in;

            if (out.IsFull()) {
                memoryBlockView.SetByte(iByte, (~byteMask & cachedByte) | (byteMask & (out.Buffer << begin)));
            }
        }
    };

    if (context.WordOrder == EWordOrder::BigEndian) {
        ForEachReverse(context.BoundMemoryBlocks, writeMemoryBlock);
    } else {
        ForEach(context.BoundMemoryBlocks, writeMemoryBlock);
    }
}
