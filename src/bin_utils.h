#pragma once

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>

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
     * @brief Convert a range of bytes starting from begin to a numeric value, assuming little-endian byte order.
     *
     * @tparam ResultType type of resulting value
     * @tparam Iterator range iterator type
     * @param begin start of range
     * @return ResultType type of result. Default implementation expects numeric type
     */
    template<class ResultType, class Iterator> ResultType GetFrom(Iterator begin)
    {
        return Get<ResultType>(begin, begin + sizeof(ResultType));
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
     * @brief Convert a range of bytes starting from begin to a numeric value, assuming big-endian byte order.
     *
     * @tparam ResultType type of resulting value
     * @tparam Iterator range iterator type
     * @param begin start of range
     * @return ResultType type of result. Default implementation expects numeric type
     */
    template<class ResultType, class Iterator> ResultType GetFromBigEndian(Iterator begin)
    {
        return GetBigEndian<ResultType>(begin, begin + sizeof(ResultType));
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
            value >>= 8; // NOLINT(clang-diagnostic-shift-count-overflow)
        }
    }

    /**
     * @brief Append a list of uint8_t values to a container using insert iterator.
     *
     * @tparam InsertIterator insert iterator type
     * @param it insert iterator
     * @param values values to append
     */
    template<class InsertIterator> void Append(InsertIterator it, std::initializer_list<uint8_t> values)
    {
        for (auto value: values) {
            *it = value;
        }
    }

    /**
     * @brief Append numeric value to a container using insert iterator.
     *        The value is appended in big endian byte order.
     *
     * @tparam InsertIterator insert iterator type
     * @tparam ValueType type of a value. Default implementation expects numeric value
     * @param it insert iterator
     * @param value value to append
     * @param byteCount number of bytes to append
     */
    template<class InsertIterator, class ValueType>
    void AppendBigEndian(InsertIterator it, ValueType value, size_t byteCount = sizeof(ValueType))
    {
        for (size_t i = byteCount - 1; i != 0; --i) {
            *it = (value >> (i * 8)) & 0xFF;
        }
        *it = value & 0xFF;
    }

    /**
     * @brief Get number with N least significant bits set to one, other bits are set to zero
     *
     * @param bitCount number of bits counting from LSB set to one
     * @return uint64_t mask value
     */
    uint64_t GetLSBMask(uint8_t bitCount);

    template<class InsertIterator>
    void ApplyByteStuffing(const std::vector<uint8_t>& data,
                           const std::unordered_map<uint8_t, std::vector<uint8_t>>& rules,
                           InsertIterator inserter)
    {
        for (auto byte: data) {
            auto it = rules.find(byte);
            if (it != rules.end()) {
                std::copy(it->second.begin(), it->second.end(), inserter);
            } else {
                *inserter = byte;
            }
        }
    }

    void DecodeByteStuffing(std::vector<uint8_t>& data, const std::unordered_map<uint8_t, std::vector<uint8_t>>& rules);
}
