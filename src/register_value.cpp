#include "register_value.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace
{
    template<typename T> T GetScalar(const std::deque<uint8_t>& value)
    {
        uint64_t retVal = 0;
        for (uint32_t i = 0; (i < sizeof(T)) && (i < value.size()); ++i) {
            retVal |= static_cast<T>(value.at(i)) << (i * 8);
        }
        return retVal;
    }
}

template<> uint64_t TRegisterValue::Get<>() const
{
    return GetScalar<uint64_t>(Value);
}

template<> int64_t TRegisterValue::Get<>() const
{
    return static_cast<int64_t>(Get<uint64_t>());
}

template<> std::vector<uint16_t> TRegisterValue::Get() const
{
    std::vector<uint16_t> vec;

    auto valueIt = Value.begin();
    while (valueIt != Value.end()) {
        uint16_t word;
        word = *valueIt;
        ++valueIt;
        if (valueIt != Value.end()) {
            word |= static_cast<uint16_t>(*valueIt) << 8;
            ++valueIt;
        }
        vec.push_back(word);
    }
    return vec;
}

template<> uint16_t TRegisterValue::Get() const
{
    return GetScalar<uint16_t>(Value);
}

template<> int16_t TRegisterValue::Get() const
{
    return static_cast<int16_t>(Get<uint16_t>());
}

template<> uint32_t TRegisterValue::Get() const
{
    return GetScalar<uint32_t>(Value);
}

template<> int32_t TRegisterValue::Get() const
{
    return static_cast<int32_t>(Get<uint32_t>());
}

template<> uint8_t TRegisterValue::Get() const
{
    return GetScalar<uint8_t>(Value);
}

template<> int8_t TRegisterValue::Get() const
{
    return static_cast<int8_t>(Get<uint8_t>());
}

template<> std::string TRegisterValue::Get() const
{
    std::string str;
    auto value = Get<std::vector<uint16_t>>();
    std::copy(std::make_reverse_iterator(value.end()),
              std::make_reverse_iterator(value.begin()),
              std::back_inserter(str));

    str.erase(std::find(str.begin(), str.end(), '\0'), str.end());
    return str;
}

TRegisterValue::TRegisterValue(uint64_t value)
{
    Set(value);
}

void TRegisterValue::Set(uint64_t value)
{
    Value.clear();
    for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
        Value.push_back(value >> (8 * i) & 0xFFU);
    }
    while (!Value.empty() && Value.back() == 0) {
        Value.pop_back();
    }
}

void TRegisterValue::Set(const std::string& value, size_t width)
{
    Value.clear();
    std::for_each(value.begin(), value.end(), [this](const auto& el) {
        Value.push_front(0);
        Value.push_front(el);
    });
    std::fill_n(std::front_inserter(Value), (width / 2 - value.size()) * 2, '\0');
}

TRegisterValue& TRegisterValue::operator=(const TRegisterValue& other)
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value = other.Value;
    return *this;
}

TRegisterValue& TRegisterValue::operator=(TRegisterValue&& other) noexcept
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value.swap(other.Value);
    return *this;
}

bool TRegisterValue::operator==(const TRegisterValue& other) const
{
    auto max = std::max(Value.size(), other.Value.size());
    for (uint32_t i = 0; i < max; ++i) {
        if ((Value.size() > i ? Value[i] : 0) != (other.Value.size() > i ? other.Value[i] : 0)) {
            return false;
        }
    }
    return true;
}

bool TRegisterValue::operator==(uint64_t other) const
{
    return *this == TRegisterValue{other};
}

bool TRegisterValue::operator!=(const TRegisterValue& other) const
{
    return !(*this == other);
}

TRegisterValue TRegisterValue::operator>>(uint32_t offset) const
{
    auto offsetInByte = offset / 8;
    auto offsetInBit = offset % 8;

    TRegisterValue result;
    auto q = Value;
    for (uint32_t i = 0; i < offsetInByte; ++i) {
        if (!q.empty()) {
            q.pop_front();
        }
    }

    if (!q.empty() && (offsetInBit != 0)) {
        for (uint32_t i = 0; i < q.size(); ++i) {
            q.at(i) >>= offsetInBit;

            if (i + 1 < q.size()) {
                q.at(i) |= q.at(i + 1) << (8 - offsetInBit);
            }
        }
    }

    result.Value = q;
    return result;
}

TRegisterValue TRegisterValue::operator<<(uint32_t offset) const
{
    auto offsetInByte = offset / 8;
    auto offsetInBit = offset % 8;

    TRegisterValue result;
    auto q = Value;

    if (offsetInBit != 0) {
        q.push_back(0);
        for (auto i = static_cast<int32_t>(q.size() - 1); i >= 0; --i) {
            q.at(i) <<= offsetInBit;
            if (i > 0) {
                q.at(i) |= q.at(i - 1) >> (8 - offsetInBit);
            }
        }
    }

    for (uint32_t i = 0; i < offsetInByte; ++i) {
        q.push_front(0);
    }

    result.Value = q;
    return result;
}

std::string TRegisterValue::ToString()
{
    std::stringstream ss;
    for (const auto& element: Value) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint16_t>(element) << " ";
    }
    return ss.str();
}

TRegisterValue& TRegisterValue::operator|=(const TRegisterValue& other)
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

void TRegisterValue::Set(const std::vector<uint8_t>& vec)
{
    Value.clear();
    std::copy(vec.begin(), vec.end(), std::back_inserter(Value));
}

void TRegisterValue::Set(const std::vector<uint16_t>& vec)
{
    Value.clear();
    std::for_each(vec.begin(), vec.end(), [this](const auto& el) {
        Value.push_back(el);
        Value.push_back(el >> 8);
    });
}