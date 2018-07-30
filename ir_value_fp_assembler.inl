#pragma once

#include "ir_value_integer_assembler.inl"

#include <string>

using namespace std;

namespace
{
    struct TIRFloatValueAssembler: TIRIntegerValueAssembler<uint32_t>
    {
        /* numeric value assembler interface */
        float GetValue() const
        {
            return *reinterpret_cast<const float*>(&Value);
        }

        void SetValue(float value)
        {
            *reinterpret_cast<float*>(&Value) = value;
        }
        /* numeric value assembler interface */
    };

    struct TIRDoubleValueAssembler: TIRIntegerValueAssembler<uint64_t>
    {
        /* numeric value assembler interface */
        double GetValue() const
        {
            return *reinterpret_cast<const double*>(&Value);
        }

        void SetValue(double value)
        {
            *reinterpret_cast<double*>(&Value) = value;
        }
        /* numeric value assembler interface */
    };
}
