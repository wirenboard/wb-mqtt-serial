#pragma once

#include "ir_value_integer_assembler.inl"

#include <string>

using namespace std;

namespace
{
    struct TIRFloatValueAssembler: TIRIntegerValueAssembler<uint32_t>
    {
        float GetValue() const
        {
            return *reinterpret_cast<const float*>(&Value);
        }

        void SetValue(float value)
        {
            *reinterpret_cast<float*>(&Value) = value;
        }
    };

    struct TIRDoubleValueAssembler: TIRIntegerValueAssembler<uint64_t>
    {
        double GetValue() const
        {
            return *reinterpret_cast<const double*>(&Value);
        }

        void SetValue(double value)
        {
            *reinterpret_cast<double*>(&Value) = value;
        }
    };
}
