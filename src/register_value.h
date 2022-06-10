#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class TRegisterValue
{
public:
    enum class ValueType
    {
        Undefined,
        Integer,
        String
    };

    TRegisterValue() = default;

    TRegisterValue(const TRegisterValue& other) = default;
    TRegisterValue(TRegisterValue&& other) = default;

    explicit TRegisterValue(uint64_t value);

    explicit TRegisterValue(const std::string& stringValue);

    void Set(uint64_t value);

    void Set(const std::string& value);

    template<class T> T Get() const;

    TRegisterValue& operator=(const TRegisterValue& other);

    TRegisterValue& operator=(TRegisterValue&& other) noexcept;

    bool operator==(const TRegisterValue& other) const;

    bool operator==(uint64_t other) const;

    bool operator!=(const TRegisterValue& other) const;

    ValueType GetType();

private:
    std::string StringValue;
    uint64_t IntegerValue{0};

    ValueType Type{ValueType::Undefined};
};