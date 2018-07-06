#pragma once
#include "ir_value_numeric_to_string.inl"
#include "ir_value_numeric_from_string.inl"

#include "register_config.h"

using namespace std;

namespace
{
    template<typename T>
    inline T RoundValue(T val, double round_to)
    {
        static_assert(is_floating_point<T>::value, "RoundValue accepts only floating point types");
        return round_to > 0 ? std::round(val / round_to) * round_to : val;
    }

    template <class T>
    struct TValueType final
    {
        using Type = decltype(declval<T>().GetValue());
    };

    template <class NumericAssembler>
    struct TIRNumericValue final: NumericAssembler
    {
        using ValueType = typename TValueType<NumericAssembler>::Type;

        string GetTextValue(const TRegisterConfig & cfg) const override
        {
            if (cfg.Scale == 1 && cfg.Offset == 0 && cfg.RoundTo == 0) {
                return ToString(NumericAssembler::GetValue());
            } else {
                auto raw = NumericAssembler::GetValue();
                auto value = static_cast<double>(raw);

                value *= cfg.Scale;                     // 1) scale value
                value += cfg.Offset;                    // 2) offset value
                value = RoundValue(value, cfg.RoundTo); // 3) round value

                return ToString<ValueType>(value);
            }
        }

        void SetTextValue(const TRegisterConfig & cfg, const string & value) override
        {
            if (cfg.Scale == 1 && cfg.Offset == 0 && cfg.RoundTo == 0) {
                NumericAssembler::SetValue(ToNumber<ValueType>(value));
            } else {
                auto fpValue = ToNumber<double>(value);

                fpValue = RoundValue(fpValue, cfg.RoundTo);
                fpValue -= cfg.Offset;
                fpValue /= cfg.Scale;

                NumericAssembler::SetValue(ToValue<ValueType>(fpValue));
            }
        }
    };
}
