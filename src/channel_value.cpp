#include "channel_value.h"
#include <vector>

TChannelValue::TChannelValue()
{}

template<> uint64_t TChannelValue::Get<>() const
{
    uint64_t retVal = 0;
    for (uint32_t i = 0; (i < sizeof(uint64_t) / sizeof(TWord)) && (i < Value.size()); ++i) {
        retVal |= static_cast<uint64_t>(Value[i]) << (i * 16);
    }
    return retVal;
}

template<> int64_t TChannelValue::Get<>() const
{
    return static_cast<int64_t>(Get<uint64_t>());
}

template<> std::vector<TWord> TChannelValue::Get() const
{
    return {Value.begin(), Value.end()};
}

template<> TWord TChannelValue::Get() const
{
    if (Value.empty())
        return 0;

    return Value.front();
}

template<> int16_t TChannelValue::Get() const
{
    return static_cast<int16_t>(Get<TWord>());
}

template<> uint32_t TChannelValue::Get() const
{
    if (Value.empty()) {
        return 0;
    } else if (Value.size() == 1) {
        return Value[0];
    }

    return (static_cast<uint32_t>(Value[1]) << 16 | Value[0]);
}

template<> int32_t TChannelValue::Get() const
{
    return static_cast<int32_t>(Get<uint32_t>());
}

template<> uint8_t TChannelValue::Get() const
{
    if (Value.empty())
        return 0;
    return Value[0] & 0xff;
}

template<> int8_t TChannelValue::Get() const
{
    return static_cast<int8_t>(Get<uint8_t>());
}

template<> std::string TChannelValue::Get() const
{
    std::string str;
    std::copy(Value.begin(),Value.end(), std::back_inserter(str));
    return str;
}

TChannelValue::TChannelValue(uint64_t value)
{
    Set(value);
}

void TChannelValue::Set(uint64_t value)
{
    Value.clear();
    for (uint32_t i = 0; i < sizeof(uint64_t) / sizeof(TWord); ++i) {
        Value.push_back(value >> (16 * i) & 0xFFFFU);
    }
    while (!Value.empty() && Value.back() == 0) {
        Value.pop_back();
    }
}

void TChannelValue::Set(const std::string& value)
{
    std::copy(value.begin(), value.end(), std::back_inserter(Value));
}

TChannelValue& TChannelValue::operator=(const TChannelValue& other)
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value = other.Value;
    return *this;
}

TChannelValue& TChannelValue::operator=(TChannelValue&& other) noexcept
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value.swap(other.Value);
    return *this;
}

bool TChannelValue::operator==(const TChannelValue& other) const
{
    return Value == other.Value;
}

bool TChannelValue::operator==(uint64_t other) const
{
    return *this == TChannelValue{other};
}

bool TChannelValue::operator!=(const TChannelValue& other) const
{
    return Value != other.Value;
}

TWord TChannelValue::PopWord()
{
    if (Value.empty())
        return 0;

    auto retVal = Value.front();
    Value.push_back(0);
    Value.pop_front();
    return retVal;
}

void TChannelValue::PushWord(TWord data)
{
    Value.push_front(data);
}

TChannelValue TChannelValue::operator>>(uint32_t offset) const
{
    auto offsetInWord = offset / (sizeof(TWord) * 8);
    auto offsetInBit = offset % (sizeof(TWord) * 8);

    TChannelValue result;
    auto q = Value;
    for (uint32_t i = 0; i < offsetInWord; ++i) {
        q.pop_front();
    }

    for (uint32_t i = 0; i < q.size(); ++i) {
        q[i] >>= offsetInBit;

        if (i + 1 < q.size()) {
            q[i] |= q[i + 1] << (sizeof(TWord) * 8 - offsetInBit);
        }
    }

    result.Value = q;
    return result;
}
