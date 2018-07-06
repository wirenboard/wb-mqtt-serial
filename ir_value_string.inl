#pragma once
#include "ir_value_array_assembler.inl"

#include <string>

namespace
{
    template <typename T>
    struct TIRGenericStringValue final: TIRArrayValueAssembler<T>
    {
        using Base = TIRArrayValueAssembler<T>;

        // g++4.7 limitation: no constructor inheritance (could be: "using Base::Base;")
        TIRGenericStringValue(const TRegisterConfig & config)
            : Base(config)
        {}

        /**
         * @note ignoring config here and thus scaling, offset and round_to
         */
        string GetTextValue(const TRegisterConfig &) const override
        {
            string result;
            result.reserve(Base::Value.size());
            auto it = Base::Value.rbegin();
            while (it != Base::Value.rend()) {
                // maybe add conversion if T is wider than 8 bits
                // by using c16rtomb/c32rtomb (UTF-16/32 to UTF-8 conversion)
                // but for now, just static_cast it

                if (it->Value) {
                    result.push_back(static_cast<char>(it->Value));
                }

                ++it;
            }

            return move(result);
        }

        /**
         * @note ignoring config here and thus scaling, offset and round_to
         */
        void SetTextValue(const TRegisterConfig &, const std::string & value) override
        {
            auto itValue = value.begin();
            auto itThisValue = Base::Value.rbegin();

            while(itThisValue != Base::Value.rend()) {
                if (itValue != value.end()) {
                    itThisValue->SetValue(*itValue);
                    ++itValue;
                } else {
                    itThisValue->SetValue(0);
                }
                ++itThisValue;
            }
        }
    };
}
