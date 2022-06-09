#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class TRegisterValue
{
public:
    TRegisterValue() = default;

    TRegisterValue(const TRegisterValue& other) = default;
    TRegisterValue(TRegisterValue&& other) = default;

    explicit TRegisterValue(uint64_t value);

    void Set(uint64_t value);

    void Set(const std::string& value);

    void Set(const std::vector<uint8_t>& vec);

    void Set(const std::vector<uint16_t>& vec);

    template<class T> T Get() const;

    TRegisterValue& operator=(const TRegisterValue& other);

    TRegisterValue& operator=(TRegisterValue&& other) noexcept;

    bool operator==(const TRegisterValue& other) const;

    bool operator==(uint64_t other) const;

    bool operator!=(const TRegisterValue& other) const;

    std::string ToString();

    std::string GetString() const{
        return StringValue;
    }

private:
    std::deque<uint8_t> Value;
    std::string StringValue;
    bool HasString{false};
};