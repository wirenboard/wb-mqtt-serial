#pragma once

#include <stdint.h>
#include <string>

/* protocol register - local bit interval that contains value for virtual register */
struct TProtocolRegisterBindInfo
{
    uint8_t             BitStart,
                        BitEnd;

    inline uint8_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    inline bool operator<(const TProtocolRegisterBindInfo & rhs)
    {
        return BitEnd <= rhs.BitStart;
    }

    std::string Describe() const;
};
