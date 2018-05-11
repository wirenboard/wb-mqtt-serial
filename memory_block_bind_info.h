#pragma once

#include "declarations.h"

#include <stdint.h>
#include <string>

/* memory block - local bit interval that contains value for virtual register */
struct TMemoryBlockBindInfo
{
    const uint16_t  BitStart,
                    BitEnd;

    TMemoryBlockBindInfo(uint16_t start, uint16_t end);
    TMemoryBlockBindInfo(const TMemoryBlockBindInfo &) = default;

    inline uint16_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    inline bool operator<(const TMemoryBlockBindInfo & rhs) const
    {
        return BitEnd <= rhs.BitStart;
    }

    uint64_t GetMask() const;
    std::string Describe() const;
};

// TODO: move somewhere else
using TBoundMemoryBlock  = std::pair<PMemoryBlock, TMemoryBlockBindInfo>;
using TBoundMemoryBlocks = TPMap<PMemoryBlock, TMemoryBlockBindInfo>;

/* Contains information needed by value extraction algorithm to extract single value from device memory view */
struct TIRDeviceValueDesc
{
    const TBoundMemoryBlocks &  BoundMemoryBlocks;
    const EWordOrder            WordOrder;
};
