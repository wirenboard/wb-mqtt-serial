#pragma once

#include "declarations.h"

#include <cstdint>
#include <string>
#include <memory>
#include <type_traits>

struct TIRValueFormatter
{
    TIRValueFormatter(const TRegisterConfig & config);
    virtual ~TIRValueFormatter() = default;

    virtual TIRValueFormatter & operator<<(uint8_t byte) = 0;
    virtual TIRValueFormatter & operator>>(uint8_t & byte) = 0;
    virtual std::string GetTextValue() const = 0;
    virtual void SetTextValue(const std::string &) = 0;
    virtual void SkipBytes(uint8_t count) = 0;
    virtual void Reset() = 0;

    static PIRValueFormatter Make(const TRegisterConfig &);

protected:
    const TRegisterConfig & Config;
    bool IsChangedByDevice;
};


struct TBitBuffer
{
    uint8_t Buffer = 0;
    uint8_t Capacity = 8;
    uint8_t Size = 0;

    TBitBuffer & operator<<(TBitBuffer & rhs)
    {
        auto bitsToTake = min(uint8_t(Capacity - Size), rhs.Size);
        Buffer <<= bitsToTake;
        Buffer |= rhs.Buffer >> (rhs.Size - bitsToTake);
        Size += bitsToTake;
        rhs.Size -= bitsToTake;
        return *this;
    }

    operator uint8_t() const
    {
        return Buffer;
    }

    operator bool() const
    {
        return Size;
    }

    bool IsFull() const
    {
        return Size == Capacity;
    }

    void Reset()
    {
        Buffer = 0;
        Capacity = 8;
        Size = 0;
    }
};
