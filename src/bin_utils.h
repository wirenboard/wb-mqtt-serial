#pragma once

#include <stddef.h>
#include <stdint.h>

namespace BinUtils
{
    /**
     * @brief Convert a range [begin, end) of bytes to a numeric value, assuming little-endian byte order.
     *
     * @tparam ResultType type of resulting value
     * @tparam Iterator range iterator type
     * @param begin start of range
     * @param end iterator pointing to a position after range's end
     * @return ResultType type of result. Default implementation expects numeric type
     */
    template<class ResultType, class Iterator> ResultType Get(Iterator begin, Iterator end)
    {
        ResultType sum = 0;
        size_t shift = 0;
        for (; begin != end; ++begin) {
            ResultType t = (*begin);
            sum += (t << shift);
            shift += 8;
        }
        return sum;
    }

    /**
     * @brief Convert a range [begin, end) of bytes to a numeric value, assuming big-endian byte order.
     *
     * @tparam ResultType type of resulting value
     * @tparam Iterator range iterator type
     * @param begin start of range
     * @param end iterator pointing to a position after range's end
     * @return ResultType type of result. Default implementation expects numeric type
     */
    template<class ResultType, class Iterator> ResultType GetBigEndian(Iterator begin, Iterator end)
    {
        ResultType sum = 0;
        for (; begin != end; ++begin) {
            sum <<= 8;
            sum += (*begin);
        }
        return sum;
    }

    /**
     * @brief Append numeric value to a container using insert iterator.
     *        The value is appended in little endian byte order.
     *
     * @tparam InsertIterator insert iterator type
     * @tparam ValueType type of a value. Default implementation expects numeric value
     * @param it insert iterator
     * @param value value to append
     * @param byteCount number of bytes to append
     */
    template<class InsertIterator, class ValueType>
    void Append(InsertIterator it, ValueType value, size_t byteCount = sizeof(ValueType))
    {
        for (size_t i = 0; i < byteCount; ++i) {
            *it = value & 0xFF;
            value >>= 8;
        }
    }

    /**
     * @brief Get number with N least signficant bits set to one, other bits are set to zero
     *
     * @param bitCount number of bits counting from LSB set to one
     * @return uint64_t mask value
     */
    uint64_t GetLSBMask(uint8_t bitCount);
}
