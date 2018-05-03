#pragma once

#include "declarations.h"

struct TIRDeviceMemoryView
{
    const uint8_t * const       RawMemory;
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;

    //TIRDeviceMemoryView(const uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize);
    virtual ~TIRDeviceMemoryView() = default;

    virtual uint64_t Get(const TIRDeviceValueDesc &) const;
    virtual void Set(const TIRDeviceValueDesc &, uint64_t value);

    const uint8_t * GetMemoryBlockData(const PMemoryBlock & memoryBlock) const;

protected:
    const uint8_t & GetByte(const PMemoryBlock & memoryBlock, uint16_t index) const;
    uint16_t GetBlockStart(const PMemoryBlock & memoryBlock) const;
};
