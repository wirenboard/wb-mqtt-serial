#pragma once

#include "ir_value_array_assembler.inl"

#include <string>

namespace
{
    /**
     * @brief encoding unaware bytestring.
     *
     * @details Though it is templated, it only reads 8 bits
     *  of every item, every bit higher than 8 is ignored.
     *  It is used for padding.
     *  To use extra bits create another string value class
     *  that packs codes that are greater than 255 to
     *  std::string according to some encodings (UTF-16 to UTF-8, etc.)
     */
    template <typename T>
    struct TIRGenericByteStringValue final: TIRArrayValueAssembler<T>
    {
        using Base = TIRArrayValueAssembler<T>;

        // g++4.7 limitation: no constructor inheritance (could be: "using Base::Base;")
        TIRGenericByteStringValue(const TRegisterConfig & config)
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
