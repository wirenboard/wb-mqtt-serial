/**
 * @brief numeric value to string conversion
 *
 * @file
 * @details ToString can convert numeric value in 3 ways:
 *  1) just via to_string:
 *   used when trying to convert integer (ToString(1))
 *
 *  2) StringFormat, precision 7:
 *   used when float is passed (ToString(2))
 *   or double that was acquired from float (ToString(5))
 *   Such behaviour is chosen because float due to lack of its precision
 *   may introduce some unwanted numbers when formatting with double precision
 *
 *  3) StringFormat, precision 15:
 *   used when double is passed (ToString(3))
 *   or double that was acquired from anything but float (ToString(4))
 */

#pragma once
#include <wbmqtt/utils.h>

#include <string>

using namespace std;

namespace
{
    /**
     * @brief ToString(1) template, default
     *  just use to_string
     *
     * @tparam T type of value
     * @param value value to convert
     * @return string converted to string value
     */
    template <typename T>
    string ToString(T value)
    {
        static_assert(is_integral<T>::value, "default ToString template is only for integers");
        return to_string(value);
    }

    /**
     * @brief ToString(2) template float specialization,
     *  formatted with limited to 7 precision
     *
     * @tparam type of value (float)
     * @param value value to convert
     * @return string converted to string value
     */
    template <>
    string ToString<float>(float value)
    {
        return StringFormat("%.7g", value);
    }

    /**
     * @brief ToString(3) template double specialization,
     *  formatted with limited to 15 precision
     *
     * @tparam type of value (double)
     * @param value value to convert
     * @return string converted to string value
     */
    template <>
    string ToString<double>(double value)
    {
        return StringFormat("%.15g", value);
    }

    /**
     * @brief ToString(4) template (double) overload, default
     *  just forward to double specialization (3)
     *
     * @tparam AcquiredFrom type that was initially converted to double
     * @param value value to convert
     * @return string converted to string value
     * @link template <> string ToString<double>(double)
     */
    template <typename AcquiredFrom>
    string ToString(double value)
    {
        return ToString(value);
    }

    /**
     * @brief ToString(5) template (double) overload float specialization,
     *  just forward to float specialization (2)
     *
     * @tparam type that was initially converted to double (float)
     * @param value value to convert
     * @return string converted to string value
     * @link template <> string ToString<float>(float)
     */
    template <>
    string ToString<float>(double value)
    {
        return ToString(static_cast<float>(value));
    }
}
