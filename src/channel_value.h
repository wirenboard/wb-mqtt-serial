#pragma once
#include <cstdint>
#include <deque>
#include <stddef.h>
#include <string>

typedef uint16_t TWord;

class TChannelValue
{
public:
    TChannelValue();

    TChannelValue(const TChannelValue& other) = default;
    TChannelValue(TChannelValue&& other) = default;

    explicit TChannelValue(uint64_t value);

    void Set(uint64_t value);

    void Set(const std::string& value);

    template<class T> T Get() const;

    TChannelValue& operator=(const TChannelValue& other);

    TChannelValue& operator=(TChannelValue&& other) noexcept;

    bool operator==(const TChannelValue& other) const;

    bool operator==(uint64_t other) const;

    bool operator!=(const TChannelValue& other) const;

    TChannelValue operator>>(uint32_t offset) const;

    void PushWord(TWord data);

    TWord PopWord();

private:
    std::deque<TWord> Value;
    uint32_t SizeInWords;
    uint32_t OffsetInBits;
};