#pragma once

#include "declarations.h"
#include "utils.h"

#include <stdint.h>
#include <string>

/* memory block - local bit interval that contains value for virtual register */
struct TIRBindInfo
{
    const uint16_t  BitStart,
                    BitEnd;

    TIRBindInfo(uint16_t start, uint16_t end);
    TIRBindInfo(const TIRBindInfo &) = default;

    inline uint16_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    inline bool operator==(const TIRBindInfo & rhs) const
    {
        return BitStart == rhs.BitStart && BitEnd == rhs.BitEnd;
    }

    inline bool operator!=(const TIRBindInfo & rhs) const
    {
        return !((*this) == rhs);
    }

    inline bool operator<(const TIRBindInfo & rhs) const
    {
        return BitEnd <= rhs.BitStart;
    }

    uint64_t GetMask() const;
    std::string Describe() const;
};

// TODO: move somewhere else
using TBoundMemoryBlock  = std::pair<PMemoryBlock, TIRBindInfo>;
using TBoundMemoryBlocks = TPMap<PMemoryBlock, TIRBindInfo>;

/* Contains information needed to access single value from device memory view */
struct TIRDeviceValueDesc
{
    const TBoundMemoryBlocks &  BoundMemoryBlocks;
    const EWordOrder            WordOrder;

    bool operator<(const TIRDeviceValueDesc & rhs) const;
};

struct TIRDeviceValueContext final: TIRDeviceValueDesc
{
    TIRValue & Value;

    TIRDeviceValueContext(const TBoundMemoryBlocks & b, const EWordOrder w, TIRValue & v)
        : TIRDeviceValueDesc{ b, w }
        , Value(v)
    {}
};
