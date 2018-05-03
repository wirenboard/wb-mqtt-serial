#pragma once

#include "declarations.h"

struct TIRDeviceMemoryViewMetadata
{
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;

    uint16_t GetBlockStart(const PMemoryBlock & memoryBlock) const;

protected:
    uint16_t GetByteIndex(const PMemoryBlock & memoryBlock, uint16_t index) const;
};

template <typename P>
struct TIRDeviceMemoryView final: TIRDeviceMemoryViewMetadata
{
    using R = std::remove_pointer<P>::type &;

    const P RawMemory;

    TIRDeviceMemoryView(P memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize)
        : TIRDeviceMemoryViewMetadata{ size, type, startAddress, blockSize }
        , RawMemory(memory)
    {}

    P GetMemoryBlockData(const PMemoryBlock & memoryBlock) const
    {
        return RawMemory + GetBlockStart(memoryBlock);
    }

    R GetByte(const PMemoryBlock & memoryBlock, uint16_t index) const
    {
        return RawMemory[GetByteIndex(memoryBlock, index)];
    }
};

using TIRDeviceMemoryViewR  = TIRDeviceMemoryView<const uint8_t *>;
using TIRDeviceMemoryViewRW = TIRDeviceMemoryView<uint8_t *>;
