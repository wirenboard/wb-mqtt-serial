#pragma once

#include "ir_value.h"

using namespace std;

namespace
{
    template <bool Reversed>
    struct TIRBCDUtility;

    /**
     * @brief Normal BCD utility
     *
     * @tparam Reversed = false
     */
    template <>
    struct TIRBCDUtility<false>
    {
        inline static uint8_t GetFactor(uint8_t byte)
        {
            return ((byte >> 4) * 10) + (byte & 0x0f);
        }

        template <typename T>
        inline static uint8_t GetByte(T value, T order)
        {
            return (((value / (order * 10)) % 10) << 4) | ((value / order) % 10);
        }
    };

    /**
     * @brief BCD with reversed nibbles utility
     *
     * @tparam Reversed = true
     */
    template <>
    struct TIRBCDUtility<true>
    {
        inline static uint8_t GetFactor(uint8_t byte)
        {
            return ((byte & 0x0f) * 10) + (byte >> 4);
        }

        template <typename T>
        inline static uint8_t GetByte(T value, T order)
        {
            return (((value / order) % 10) << 4) | ((value / (order * 10)) % 10);
        }
    };

    template <typename T, bool IsReversed = false, uint8_t S = sizeof(T)>
    struct TIRBCDValueAssembler: TIRValue
    {
        static_assert(std::is_integral<T>::value, "non-intergral type for BCD value assembler");

        T Value;

        /* utility */
        using Utility = TIRBCDUtility<IsReversed>;

        inline static T GetOrder(TByteIndex iByte)
        {
            return pow10(iByte * 2);
        }

        inline static bool IsValidByte(uint8_t byte)
        {
            return ((byte & 0x0f) <= 9) && ((byte >> 4) <= 9);
        }
        /* utility */

        /* assembler implementation */
        uint8_t GetByte(TByteIndex iByte) const final
        {
            return Utility::GetByte(Value, GetOrder(iByte));
        }

        void SetByte(uint8_t byte, TByteIndex iByte) final
        {
            if (!IsValidByte(byte)) {
                auto flags = cerr.flags();
                cerr << "WARNING: invalid BCD byte at SetByte: "
                     << "0x" << hex << (int)byte << ". Skipping" << endl;
                cerr.flags(flags);
                return;
            }

            auto order = GetOrder(iByte);
            auto oldFactor = Utility::GetFactor(Utility::GetByte(Value, order));
            auto newFactor = Utility::GetFactor(byte);

            if (oldFactor != newFactor) {
                Value += order * (newFactor - oldFactor);
                Changed = true;
            }
        }

        void Assign(const TIRValue & rhs) final
        {
            Value = dynamic_cast<const TIRBCDValueAssembler &>(rhs).Value;
        }
        /* assembler implementation */

        /* static numeric interface */
        T GetValue() const { return Value; }

        void SetValue(T value) { Value = value; }
        /* static numeric interface */
    };
}
