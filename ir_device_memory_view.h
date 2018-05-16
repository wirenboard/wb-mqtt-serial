#pragma once

#include "declarations.h"

#include <cassert>

struct TIRDeviceMemoryBlockView
{
    uint8_t * const     RawMemory;
    const CPMemoryBlock MemoryBlock;
    const bool          Readonly;

    TIRDeviceMemoryBlockView(const TIRDeviceMemoryBlockView &) = default;

    inline operator bool() const
    {
        return bool(RawMemory);
    }

    inline operator const uint8_t*() const
    {
        return RawMemory;
    }

    uint16_t GetByteIndex(uint16_t index) const;
    uint16_t GetValueByteIndex(uint16_t index) const;
    uint16_t GetValueSize(uint16_t index) const;

    uint8_t GetByte(uint16_t index) const;
    void SetByte(uint16_t index, uint8_t) const;
    uint64_t GetValue(uint16_t index) const;
    void SetValue(uint16_t index, uint64_t value) const;
};

struct TIRDeviceMemoryView
{
    uint8_t * const             RawMemory;
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;
    const bool                  Readonly;

    TIRDeviceMemoryView(uint8_t * memory, uint32_t size, const TMemoryBlockType &, uint32_t start, uint16_t blockSize, bool readonly = false);
    TIRDeviceMemoryView(const uint8_t * memory, uint32_t size, const TMemoryBlockType &, uint32_t start, uint16_t blockSize);

    void Clear() const;

    uint16_t GetBlockStart(const CPMemoryBlock & memoryBlock) const;

    uint64_t ReadValue(const TIRDeviceValueDesc &) const;
    void WriteValue(const TIRDeviceValueDesc &, uint64_t value) const;

    inline TIRDeviceMemoryBlockView operator[](const CPMemoryBlock & memoryBlock) const
    {
        return { RawMemory + GetBlockStart(memoryBlock), memoryBlock, Readonly };
    }
};
