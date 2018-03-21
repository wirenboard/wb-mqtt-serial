#pragma once

#include <stdint.h>
#include <string>

/* protocol register - local bit interval that contains value for virtual register */
struct TProtocolRegisterBindInfo
{
    const uint8_t   BitStart,
                    BitEnd;

    TProtocolRegisterBindInfo(uint8_t start, uint8_t end);
    TProtocolRegisterBindInfo(const TProtocolRegisterBindInfo &) = default;

    inline uint8_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    inline bool operator<(const TProtocolRegisterBindInfo & rhs) const
    {
        return BitEnd <= rhs.BitStart;
    }

    std::string Describe() const;
};
