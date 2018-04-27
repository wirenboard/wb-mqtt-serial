#pragma once

#include "declarations.h"

#include <stdint.h>
#include <string>

/* protocol register - local bit interval that contains value for virtual register */
struct TProtocolRegisterBindInfo
{
    const uint16_t  BitStart,
                    BitEnd;

    TProtocolRegisterBindInfo(uint16_t start, uint16_t end);
    TProtocolRegisterBindInfo(const TProtocolRegisterBindInfo &) = default;

    inline uint16_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    inline bool operator<(const TProtocolRegisterBindInfo & rhs) const
    {
        return BitEnd <= rhs.BitStart;
    }

    std::string Describe() const;
};

// TODO: move somewhere else
using TBoundMemoryBlock  = std::pair<PProtocolRegister, TProtocolRegisterBindInfo>;
using TBoundMemoryBlocks = TPMap<PProtocolRegister, TProtocolRegisterBindInfo>;

/* Contains information needed by memory view to extract single value */
struct TIRDeviceValueDesc
{
    const TBoundMemoryBlocks & BoundMemoryBlocks;
    const EWordOrder           WordOrder;
};
