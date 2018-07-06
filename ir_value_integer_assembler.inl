#pragma once

#include "ir_value.h"

using namespace std;

namespace
{
    template <typename T, uint8_t S = sizeof(T)>
    struct TIRIntegerValueAssembler: TIRValue
    {
        static_assert(std::is_integral<T>::value, "non-intergral type for integer value assembler");
        static_assert(S <= sizeof(T), "custom size must be less or equal to size of stored value");

        T Value = {};

        void Assign(const TIRValue & rhs) final
        {
            Value = dynamic_cast<const TIRIntegerValueAssembler &>(rhs).Value;
        }

        uint8_t GetByte(TByteIndex iByte) const final
        {
            return Value >> (iByte * 8);
        }

        void SetByte(uint8_t newByte, TByteIndex iByte) final
        {
            auto oldByte = GetByte(iByte);
            if (oldByte != newByte) {
                Value ^= static_cast<T>(oldByte) << iByte * 8;  // set all old bits to 0
                Value |= static_cast<T>(newByte) << iByte * 8;  // set new bits
                Changed = true;
            }
        }

        string DescribeShort() const final
        {
            stringstream ss;
            ss << (uint64_t)Value;
            return ss.str();
        }

        /* static interface */
        T GetValue() const
        {
            return Value;
        }

        void SetValue(T value)
        {
            Value = value;
        }
        /* static interface */
    };

    struct TIRS24ValueAssembler: TIRIntegerValueAssembler<int32_t, 3>
    {
        int32_t GetValue() const
        {
            uint32_t v = Value & 0xffffff;
            if (v & 0x800000) // fix sign (TBD: test)
                v |= 0xff000000;
            return v;
        }

        void SetValue(int32_t value)
        {
            Value = value & 0xffffff;
        }
    };
}
