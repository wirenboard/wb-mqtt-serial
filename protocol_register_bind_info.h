#pragma once

#include <stdint.h>
#include <string>

/* protocol register - local bit interval that contains value for virtual register */
struct TProtocolRegisterBindInfo
{
    enum EReadState {NotRead, ReadOk, ReadError};

    uint8_t             BitStart,
                        BitEnd;
    mutable EReadState  IsRead = NotRead;

    inline uint8_t BitCount() const
    {
        return BitEnd - BitStart;
    }

    std::string Describe() const;
};
