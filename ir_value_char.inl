#pragma once

#include "ir_value_integer_assembler.inl"

#include <string>

using namespace std;

namespace
{
    struct TIRChar8Value final: TIRIntegerValueAssembler<char>
    {
        string GetTextValue(const TRegisterConfig &) const override
        {
            return { Value };
        }

        void SetTextValue(const TRegisterConfig &, const std::string & value) override
        {
            Value = value.empty() ? 0 : value.front();
        }
    };
}
