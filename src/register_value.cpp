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

void TRegisterValue::Set(const std::string& value)
{
    HasString = true;
    StringValue = value;
}

TRegisterValue& TRegisterValue::operator=(const TRegisterValue& other)
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value = other.Value;
    StringValue = other.StringValue;
    return *this;
}

TRegisterValue& TRegisterValue::operator=(TRegisterValue&& other) noexcept
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Value.swap(other.Value);
    StringValue.swap(other.StringValue);
    return *this;
}

bool TRegisterValue::operator==(const TRegisterValue& other) const
{
    if (HasString) {
        return (StringValue == other.StringValue);
    }
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

std::string TRegisterValue::ToString()
{
    std::stringstream ss;
    for (const auto& element: Value) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint16_t>(element) << " ";
    }
    return ss.str();
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