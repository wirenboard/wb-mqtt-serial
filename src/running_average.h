#pragma once
#include <stddef.h>
#include <stdint.h>

template<class ValueType, size_t WindowSize> class TRunningAverage
{
    ValueType Average;

public:
    TRunningAverage(const ValueType& initialValue): Average(initialValue)
    {}

    const ValueType& GetValue() const
    {
        return Average;
    }

    void AddValue(const ValueType& value)
    {
        Average = ((WindowSize - 1) * Average + value) / WindowSize;
    }
};

template<class ValueType> class TRunningAverage<ValueType, 0>
{
public:
    TRunningAverage()
    {}
};
