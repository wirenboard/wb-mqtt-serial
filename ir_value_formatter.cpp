#include "ir_value_formatter.h"
#include "register_config.h"

#include <wbmqtt/utils.h>

#include <cassert>

using namespace std;

namespace // implementation
{
    template<typename T>
    inline T RoundValue(T val, double round_to)
    {
        static_assert(std::is_floating_point<T>::value, "TRegisterHandler::RoundValue accepts only floating point types");
        return round_to > 0 ? std::round(val / round_to) * round_to : val;
    }

    template<typename T>
    enable_if<is_signed<T>::value, T>::
    type _ToInteger(const string & value)
    {
        return stoll(value);
    }

    template<typename T>
    enable_if<is_unsigned<T>::value, T>::
    type _ToInteger(const string & value)
    {
        return stoull(value);
    }

    template<typename T>
    T ToInteger(const string & value)
    {
        static_assert(is_integral<T>::value, "ToInteger can convert only to integers");
        return _ToInteger<T>(value);
    }

    template <typename T, uint8_t S = sizeof(T)>
    struct TIRIntegerValueFormatter: TIRValueFormatter
    {
        using Base = TIRValueFormatter;
        using Base::Base;

        static_assert(std::is_integral<T>::value, "non-intergral type for integer value formatter");
        static_assert(S <= sizeof(T), "custom width must me less or equal to with of stored value");

        T Value = {};
        uint8_t Size = 0;

        TIRValueFormatter & operator<<(uint8_t byte) override
        {
            ++Size;
            assert(Size <= S);
            Value <<= 8;
            Value |= byte;
        }

        TIRValueFormatter & operator>>(uint8_t & byte) override
        {
            byte = Value;
            SkipBytes(1);
        }

        string GetTextValue() const override
        {
            return to_string(Value);
        }

        void SetTextValue(const std::string & value) override
        {
            Value = ToInteger(value);
            Size = S;
        }

        void SkipBytes(uint8_t count) override
        {
            assert(Size >= count);
            Value >>= count * 8;
            Size -= count;
        }

        void Reset() override
        {
            Base::Reset();
            Value = {};
            Size = 0;
        }
    };

    using TIRS8ValueFormatter  = TIRIntegerValueFormatter<int8_t>;
    using TIRU8ValueFormatter  = TIRIntegerValueFormatter<uint8_t>;
    using TIRS16ValueFormatter = TIRIntegerValueFormatter<int16_t>;
    using TIRU16ValueFormatter = TIRIntegerValueFormatter<uint16_t>;
    using TIRU24ValueFormatter = TIRIntegerValueFormatter<uint32_t, 3>;
    using TIRS32ValueFormatter = TIRIntegerValueFormatter<int32_t>;
    using TIRU32ValueFormatter = TIRIntegerValueFormatter<uint32_t>;
    using TIRS64ValueFormatter = TIRIntegerValueFormatter<int64_t>;
    using TIRU64ValueFormatter = TIRIntegerValueFormatter<uint64_t>;

    struct TIRChar8ValueFormatter final: TIRS8ValueFormatter
    {
        using TIRS8ValueFormatter::TIRS8ValueFormatter;

        string GetTextValue() const override
        {
            return { 1, Value };
        }
    };

    struct TIRS24ValueFormatter final: TIRIntegerValueFormatter<int32_t, 3>
    {
        using TIRIntegerValueFormatter::TIRIntegerValueFormatter;

        string GetTextValue() const override
        {
            if (Value & 0x800000) // fix sign (TBD: test)
                return to_string(-(Value & 0x7fffff));
            return to_string(Value);
        }
    };

    struct TIRFloatValueFormatter final: TIRU32ValueFormatter
    {
        using TIRU32ValueFormatter::TIRU32ValueFormatter;

        string GetTextValue() const override
        {
            auto value = *reinterpret_cast<const float*>(&Value);

            return StringFormat("%.7g",
                RoundValue(Config.Scale * value + Config.Offset, Config.RoundTo)
            );
        }
    };

    struct TIRDoubleValueFormatter final: TIRU64ValueFormatter
    {
        using TIRU64ValueFormatter::TIRU64ValueFormatter;

        string GetTextValue() const override
        {
            auto value = *reinterpret_cast<const double*>(&Value);

            return StringFormat("%.15g",
                RoundValue(Config.Scale * value + Config.Offset, Config.RoundTo)
            );
        }
    };

    template <typename T, uint8_t S = sizeof(T)>
    struct TIRBCDValueFormatter: TIRValueFormatter
    {
        using Base = TIRValueFormatter;
        using Base::Base;

        static_assert(std::is_integral<T>::value, "non-intergral type for BCD value formatter");

        vector<char> Value

        TIRValueFormatter & operator<<(uint8_t byte) override
        {
            Value.push_back('0' + (byte >> 4));
            Value.push_back('0' + (byte & 0x0f));
            return *this;
        }

        TIRValueFormatter & operator>>(uint8_t & byte) override
        {
            byte |= Value.back() - '0';
            Value.pop_back();
            byte |= (Value.back() - '0') << 4;
            Value.pop_back();
            return *this;
        }

        string GetTextValue() const override
        {
            return { Value.begin(), Value.end() };
        }

        void SetTextValue(const std::string & value) override
        {
            Reset();
            copy(value.begin(), value.end(), Value.begin());
        }

        void SkipBytes(uint8_t count) override
        {
            assert(Value.size() >= count);
            Value.resize(Value.size() - count);
        }

        void Reset() override
        {
            Value.clear();
        }
    };

    using TIRBCD8ValueFormatter  = TIRBCDValueFormatter<uint8_t>;
    using TIRBCD16ValueFormatter = TIRBCDValueFormatter<uint16_t>;
    using TIRBCD24ValueFormatter = TIRBCDValueFormatter<uint32_t, 3>;
    using TIRBCD32ValueFormatter = TIRBCDValueFormatter<uint32_t>;

    struct TIRStringValueFormatter final: TIRValueFormatter
    {
        using Base = TIRValueFormatter;
        using Base::Base;

        vector<char> Value;

        TIRValueFormatter & operator<<(uint8_t byte) override
        {
            Value.push_back(byte);
            return *this;
        }

        TIRValueFormatter & operator>>(uint8_t & byte) override
        {
            byte = Value.back();
            Value.pop_back();
            return *this;
        }

        string GetTextValue() const override
        {
            return { Value.begin(), Value.end() };
        }

        void SetTextValue(const std::string & value) override
        {
            Reset();
            copy(value.begin(), value.end(), Value.begin());
        }

        void SkipBytes(uint8_t count) override
        {
            assert(Value.size() >= count);
            Value.resize(Value.size() - count);
        }

        void Reset() override
        {
            Base::Reset();
            Value.clear();
        }
    };

} // implementation

// interface
TIRValueFormatter::TIRValueFormatter(const TRegisterConfig & config)
    : Config(config)
{}

void TIRValueFormatter::Reset()
{
    IsChangedByDevice = false;
}

PIRValueFormatter TIRValueFormatter::Make(const TRegisterConfig & config)
{
    switch(config.Format) {
        case U8: return MakeUnique<TIRU8ValueFormatter>(config);
        case S8: return MakeUnique<TIRS8ValueFormatter>(config);
        case U16: return MakeUnique<TIRU16ValueFormatter>(config);
        case S16: return MakeUnique<TIRS16ValueFormatter>(config);
        case S24: return MakeUnique<TIRS24ValueFormatter>(config);
        case U24: return MakeUnique<TIRU24ValueFormatter>(config);
        case U32: return MakeUnique<TIRU32ValueFormatter>(config);
        case S32: return MakeUnique<TIRS32ValueFormatter>(config);
        case S64: return MakeUnique<TIRS64ValueFormatter>(config);
        case U64: return MakeUnique<TIRU64ValueFormatter>(config);
        case BCD8: return MakeUnique<TIRBCD8ValueFormatter>(config);
        case BCD16: return MakeUnique<TIRBCD16ValueFormatter>(config);
        case BCD24: return MakeUnique<TIRBCD24ValueFormatter>(config);
        case BCD32: return MakeUnique<TIRBCD32ValueFormatter>(config);
        case Float: return MakeUnique<TIRFloatValueFormatter>(config);
        case Double: return MakeUnique<TIRDoubleValueFormatter>(config);
        case Char8: return MakeUnique<TIRChar8ValueFormatter>(config);
        case String: return MakeUnique<TIRStringValueFormatter>(config);
    }
}
// interface
