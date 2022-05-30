#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

typedef uint16_t TRegisterWord;

class TRegisterValue
{
public:
    TRegisterValue() = default;

    TRegisterValue(const TRegisterValue& other) = default;
    TRegisterValue(TRegisterValue&& other) = default;

    TRegisterValue(uint64_t value);

    void Set(uint64_t value);

    void Set(const std::string& value, size_t width);

    void Set(const std::vector<uint8_t>& vec);

    void Set(const std::vector<TRegisterWord>& vec);

    template<class T> T Get() const;

    TRegisterValue& operator=(const TRegisterValue& other);

    TRegisterValue& operator=(TRegisterValue&& other) noexcept;

    bool operator==(const TRegisterValue& other) const;

    bool operator==(uint64_t other) const;

    bool operator!=(const TRegisterValue& other) const;

    TRegisterValue operator>>(uint32_t offset) const;

    TRegisterValue operator<<(uint32_t offset) const;

    TRegisterValue& operator|=(const TRegisterValue& other);

    std::string ToString();

private:
    std::deque<uint8_t> Value;
};