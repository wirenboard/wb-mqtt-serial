#pragma once

#include "ir_value_numeric.inl"

#include <cassert>

using namespace std;

namespace
{
    template <typename T, class Assembler = TIRIntegerValueAssembler<T>>
    struct TIRArrayValueAssembler: TIRValue
    {
        static constexpr TValueSize ItemSize = sizeof(T);
        static constexpr TValueSize ItemWidth = ItemSize * 8;

        vector<TIRNumericValue<Assembler>> Value;

        TIRArrayValueAssembler(const TRegisterConfig & config)
            : Value(BitCountToRegCount(config.GetWidth(), ItemWidth))
        {}

        uint8_t GetByte(TByteIndex iByte) const final
        {
            auto index = iByte / ItemSize;
            iByte %= ItemSize;

            return Value[index].GetByte(iByte);
        }

        void SetByte(uint8_t byte, TByteIndex iByte) final
        {
            auto index = iByte / ItemSize;
            iByte %= ItemSize;

            auto & itemFormatter = Value[index];
            itemFormatter.SetByte(byte, iByte);

            Changed |= itemFormatter.IsChanged();
            itemFormatter.ResetChanged();
        }

        void Assign(const TIRValue & rhs) final
        {
            const auto & rhsValue = dynamic_cast<const TIRArrayValueAssembler &>(rhs).Value;

            assert(rhsValue.size() == Value.size());

            auto itThisValue = Value.begin();
            auto itRhsValue = rhsValue.begin();

            while(itThisValue != Value.end() && itRhsValue != rhsValue.end()) {
                itThisValue->Assign(*itRhsValue);

                ++itThisValue;
                ++itRhsValue;
            }
        }
    };
}
