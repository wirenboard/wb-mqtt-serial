#pragma once

#include "declarations.h"

struct TIRDeviceMemoryBlockViewMetadata
{
    const CPMemoryBlock MemoryBlock;

    uint16_t GetByteIndex(uint16_t index) const;
};

template <typename P>
struct TIRDeviceMemoryBlockView final: TIRDeviceMemoryBlockViewMetadata
{
    using R = std::remove_pointer<P>::type &;

    const P RawMemory;

    TIRDeviceMemoryBlockView(P memory, const CPMemoryBlock & memoryBlock)
        : TIRDeviceMemoryBlockViewMetadata{ memoryBlock }
        , RawMemory(memory)
    {}

    TIRDeviceMemoryBlockView(const TIRDeviceMemoryBlockView &) = default;

    TIRDeviceMemoryBlockView & operator=(const TIRDeviceMemoryBlockView & other)
    {
        if (this != &other) {
             MemoryBlock = other.MemoryBlock;
             RawMemory = other.RawMemory;
        }

        return *this;
    }

    R operator[](uint16_t index) const
    {
        assert(RawMemory);

        return RawMemory[GetByteIndex(index)];
    }

    operator bool() const
    {
        return bool(RawMemory);
    }

    operator P() const
    {
        return RawMemory;
    }
};

struct TIRDeviceMemoryViewMetadata
{
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;

    uint16_t GetBlockStart(const CPMemoryBlock & memoryBlock) const;
};

template <typename P>
struct TIRDeviceMemoryView final: TIRDeviceMemoryViewMetadata
{
    const P RawMemory;

    TIRDeviceMemoryView(P memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize)
        : TIRDeviceMemoryViewMetadata{ size, type, startAddress, blockSize }
        , RawMemory(memory)
    {}

    TIRDeviceMemoryBlockView<P> operator[](const CPMemoryBlock & memoryBlock) const
    {
        return { RawMemory + GetBlockStart(memoryBlock), memoryBlock };
    }
};

using TIRDeviceMemoryBlockViewR  = TIRDeviceMemoryBlockView<const uint8_t *>;
using TIRDeviceMemoryBlockViewRW = TIRDeviceMemoryBlockView<uint8_t *>;

using TIRDeviceMemoryViewR  = TIRDeviceMemoryView<const uint8_t *>;
using TIRDeviceMemoryViewRW = TIRDeviceMemoryView<uint8_t *>;
