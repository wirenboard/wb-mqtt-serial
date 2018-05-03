#pragma once

#include "declarations.h"

#include <stdint.h>
#include <string>

/* protocol register - local bit interval that contains value for virtual register */
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

    std::string Describe() const;
};

// TODO: move somewhere else
using TBoundMemoryBlock  = std::pair<PMemoryBlock, TMemoryBlockBindInfo>;
using TBoundMemoryBlocks = TPMap<PMemoryBlock, TMemoryBlockBindInfo>;

/* Contains information needed by memory view to extract single value */
struct TIRDeviceValueDesc
{
    const TBoundMemoryBlocks & BoundMemoryBlocks;
    const EWordOrder           WordOrder;
};
