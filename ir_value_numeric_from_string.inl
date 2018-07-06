#pragma once

#include <string>

using namespace std;

namespace
{
    template <typename T>
    typename enable_if<is_integral<T>::value, T>::
    type ToValue(double value)
    {
        return static_cast<T>(round(value));
    }

    template <typename T>
    typename enable_if<is_floating_point<T>::value, T>::
    type ToValue(double value)
    {
        return static_cast<T>(value);
    }

    template<typename T>
    typename enable_if<is_signed<T>::value, T>::
    type ToInteger(const string & value)
    {
        static_assert(is_integral<T>::value, "ToInteger can convert only to integers");
        return stoll(value);
    }

    template<typename T>
    typename enable_if<is_unsigned<T>::value, T>::
    type ToInteger(const string & value)
    {
        static_assert(is_integral<T>::value, "ToInteger can convert only to integers");
        return stoull(value);
    }

    template <typename T>
    T ToFloatingPoint(const string & value);

    template <>
    float ToFloatingPoint<float>(const string & value)
    {
        return stof(value);
    }

    template <>
    double ToFloatingPoint<double>(const string & value)
    {
        return stod(value);
    }

    template <typename T>
    typename enable_if<is_integral<T>::value, T>::
    type ToNumber(const string & value)
    {
        return ToInteger<T>(value);
    }

    template <typename T>
    typename enable_if<is_floating_point<T>::value, T>::
    type ToNumber(const string & value)
    {
        return ToFloatingPoint<T>(value);
    }
}
