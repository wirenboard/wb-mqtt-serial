#include "channel_value.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

TChannelValue::TChannelValue()
{}

template<> uint64_t TChannelValue::Get<>() const
{
    uint64_t retVal = 0;
    for (uint32_t i = 0; (i < sizeof(uint64_t) / sizeof(TRegisterWord)) && (i < Value.size()); ++i) {
        retVal |= static_cast<uint64_t>(Value.at(i)) << (i * 16);
    }
    return retVal;
}

template<> int64_t TChannelValue::Get<>() const
{
    return static_cast<int64_t>(Get<uint64_t>());
}

template<> std::vector<TRegisterWord> TChannelValue::Get() const
{
    return {Value.begin(), Value.end()};
}

template<> TRegisterWord TChannelValue::Get() const
{
    if (Value.empty())
        return 0;

    return Value.front();
}

template<> int16_t TChannelValue::Get() const
{
    return static_cast<int16_t>(Get<TRegisterWord>());
}

template<> uint32_t TChannelValue::Get() const
{
    if (Value.empty()) {
        return 0;
    } else if (Value.size() == 1) {
        return Value.at(0);
    }

    return (static_cast<uint32_t>(Value.at(1)) << 16 | Value.at(0));
}

template<> int32_t TChannelValue::Get() const
{
    return static_cast<int32_t>(Get<uint32_t>());
}

template<> uint8_t TChannelValue::Get() const
{
    if (Value.empty())
        return 0;
    return Value.at(0) & 0xff;
}

template<> int8_t TChannelValue::Get() const
{
    return static_cast<int8_t>(Get<uint8_t>());
}

template<> std::string TChannelValue::Get() const
{
    std::string str;
    std::copy(std::make_reverse_iterator(Value.end()),
              std::make_reverse_iterator(Value.begin()),
              std::back_inserter(str));

    str.erase(std::find(str.begin(), str.end(), '\0'), str.end());
    return str;
}

TChannelValue::TChannelValue(uint64_t value)
{
    Set(value);
}

void TChannelValue::Set(uint64_t value)
{
    Value.clear();
    for (uint32_t i = 0; i < sizeof(uint64_t) / sizeof(TRegisterWord); ++i) {
        Value.push_back(value >> (16 * i) & 0xFFFFU);
    }
    while (!Value.empty() && Value.back() == 0) {
        Value.pop_back();
    }
}

void TChannelValue::Set(const std::string& value)
{
    std::copy(value.begin(), value.end(), std::front_inserter(Value));
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

TRegisterWord TChannelValue::PopWord()
{
    if (Value.empty())
        return 0;

    auto retVal = Value.front();
    Value.push_back(0);
    Value.pop_front();
    return retVal;
}

void TChannelValue::PushWord(TRegisterWord data)
{
    Value.push_front(data);
}

TChannelValue TChannelValue::operator>>(uint32_t offset) const
{
    auto offsetInWord = offset / (sizeof(TRegisterWord) * 8);
    auto offsetInBit = offset % (sizeof(TRegisterWord) * 8);

    TChannelValue result;
    auto q = Value;
    for (uint32_t i = 0; i < offsetInWord; ++i) {
        q.pop_front();
    }

    if (offsetInBit != 0) {
        for (uint32_t i = 0; i < q.size(); ++i) {
            q.at(i) >>= offsetInBit;

            if (i + 1 < q.size()) {
                q.at(i) |= q.at(i + 1) << (sizeof(TRegisterWord) * 8 - offsetInBit);
            }
        }
    }

    result.Value = q;
    return result;
}

TChannelValue TChannelValue::operator<<(uint32_t offset) const
{
    auto offsetInWord = offset / (sizeof(TRegisterWord) * 8);
    auto offsetInBit = offset % (sizeof(TRegisterWord) * 8);

    TChannelValue result;
    auto q = Value;

    if (offsetInBit != 0) {
        q.push_back(0);
        for (int32_t i = (q.size() - 1); i >= 0; --i) {
            q.at(i) <<= offsetInBit;
            if (i > 0) {
                q.at(i) |= q.at(i - 1) >> (sizeof(TRegisterWord) * 8 - offsetInBit);
            }
        }
    }

    for (uint32_t i = 0; i < offsetInWord; ++i) {
        q.push_front(0);
    }

    result.Value = q;
    return result;
}

std::string TChannelValue::ToString()
{
    std::stringstream ss;
    for (const auto& element: Value) {
        ss << std::hex << std::setw(4) << std::setfill('0') << element << " ";
    }
    return ss.str();
}

TChannelValue& TChannelValue::operator|=(const TChannelValue& other)
{
    // Guard self assignment
    if (this == &other)
        return *this;

    auto size = Value.size();
    for (uint32_t i = 0; i < other.Value.size(); ++i) {
        if (i >= size) {
            Value.push_back(other.Value.at(i));
        } else {
            Value.at(i) |= other.Value.at(i);
        }
    }

    return *this;
}
