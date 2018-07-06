#pragma once

#include "declarations.h"

/**
 * @brief Helper class for memory view
 *
 * @details Used to assemble bytes by bits and vice versa
 */
struct TBitBuffer
{
    uint8_t Buffer;
    uint8_t Capacity;
    uint8_t Size;

    TBitBuffer(uint8_t buffer = 0, uint8_t capacity = 8, uint8_t size = 0)
        : Buffer(buffer)
        , Capacity(capacity)
        , Size(size)
    {}

    inline operator uint8_t() const
    {
        return Buffer;
    }

    inline operator bool() const
    {
        return Size;
    }

    inline bool IsFull() const
    {
        return Size == Capacity;
    }

    inline void Reset()
    {
        Buffer = 0;
        Capacity = 8;
        Size = 0;
    }
};


/**
 * @brief move bits from left bit buffer to right
 *  till left buffer is empty or right buffer is full
 *
 * @details operates in such way that lower bits
 *  of left buffer will move to right buffer as higher:
 *  before: lhs: [--][--][x5][x4][x3][x2][x1][x0] rhs: [--][--][--][--][y3][y2][y1][y0]
 *  after:  lhs: [--][--][--][--][--][--][x5][x4] rhs: [x3][x2][x1][x0][y3][y2][y1][y0]
 * @param rhs receiver of bits
 * @return TBitBuffer&
 */
inline TBitBuffer & operator>>(TBitBuffer & lhs, TBitBuffer & rhs)
{
    auto bitsToTake = std::min(uint8_t(rhs.Capacity - rhs.Size), lhs.Size);
    rhs.Buffer |= lhs.Buffer << rhs.Size;
    lhs.Buffer >>= bitsToTake;
    rhs.Size += bitsToTake;
    lhs.Size -= bitsToTake;
    return lhs;
}
