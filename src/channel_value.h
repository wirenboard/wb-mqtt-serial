#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

typedef uint16_t TRegisterWord;

class TChannelValue
{
public:
    TChannelValue() = default;

    TChannelValue(const TChannelValue& other) = default;
    TChannelValue(TChannelValue&& other) = default;

    explicit TChannelValue(uint64_t value);

    void Set(uint64_t value);

    void Set(const std::string& value, size_t width);

    template<class T> T Get() const;

    TChannelValue& operator=(const TChannelValue& other);

    TChannelValue& operator=(TChannelValue&& other) noexcept;

    bool operator==(const TChannelValue& other) const;

    bool operator==(uint64_t other) const;

    bool operator!=(const TChannelValue& other) const;

    TChannelValue operator>>(uint32_t offset) const;

    TChannelValue operator<<(uint32_t offset) const;

    TChannelValue& operator|=(const TChannelValue& other);

    void PushWord(TRegisterWord data);

    TRegisterWord PopWord();

    std::string ToString();

private:
    std::deque<TRegisterWord> Value;
};