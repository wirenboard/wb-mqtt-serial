#pragma once
#include <cstdint>

namespace Register
{
    //    typedef uint64_t TValue;
    class TValue
    {
    public:
        TValue()
        {
            Value.resize(8);
        }

        TValue(const TValue& other) = default;
        TValue(TValue&& other) = default;

        void Set(uint64_t value)
        {
            Value[0] = value & 0xFFU;
            Value[1] = (value >> (8 * 1)) & 0xFFU;
            Value[2] = (value >> (8 * 2)) & 0xFFU;
            Value[3] = (value >> (8 * 3)) & 0xFFU;
            Value[4] = (value >> (8 * 4)) & 0xFFU;
            Value[5] = (value >> (8 * 5)) & 0xFFU;
            Value[6] = (value >> (8 * 6)) & 0xFFU;
            Value[7] = (value >> (8 * 7)) & 0xFFU;
        }

        template<class T> T Get() const;

        template<> uint64_t Get() const
        {
            uint64_t retVal = 0;
            for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
                retVal |= Value[i] << (i * 8);
            }
            return retVal;
        }

        template<> std::vector<uint8_t> Get() const
        {
            return Value;
        }

        template<> uint16_t Get() const
        {
            uint16_t retVal = 0;
            for (uint32_t i = 0; i < sizeof(uint16_t); ++i) {
                retVal |= Value[i] << (i * 8);
            }
            return retVal;
        }

        TValue& operator=(const TValue& other)
        {
            // Guard self assignment
            if (this == &other)
                return *this;

            Value = other.Value;
            return *this;
        }

        TValue& operator=(TValue&& other) noexcept
        {
            // Guard self assignment
            if (this == &other)
                return *this;

            Value.swap(other.Value);
            return *this;
        }

        bool operator==(const TValue& other) const
        {
            return Value == other.Value;
        }

        bool operator!=(const TValue& other) const
        {
            return Value != other.Value;
        }

    private:
        std::vector<uint8_t> Value;
    };

}
