#pragma once

#include "declarations.h"
#include "types.h"


struct TRegisterConfig : public std::enable_shared_from_this<TRegisterConfig>
{
    TRegisterConfig(int type, int address,
                    ERegisterFormat format, double scale, double offset,
                    double round_to, bool poll, bool readonly,
                    const std::string& type_name,
                    bool has_error_value, uint64_t error_value,
                    const EWordOrder word_order, uint8_t bit_offset, uint8_t bit_width);

    static PRegisterConfig Create(int type = 0, int address = 0,
                                  ERegisterFormat format = U16, double scale = 1, double offset = 0,
                                  double round_to = 0, bool poll = true, bool readonly = false,
                                  const std::string& type_name = "",
                                  bool has_error_value = false,
                                  uint64_t error_value = 0,
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  uint8_t bit_offset = 0, uint8_t bit_width = 0);

    uint8_t GetBitWidth() const;
    uint8_t ByteWidth() const;
    uint8_t Width() const;

    std::string ToString() const;

    int Type;
    int Address;
    ERegisterFormat Format;
    double Scale;
    double Offset;
    double RoundTo;
    bool Poll;
    bool ReadOnly;
    std::string TypeName;
    std::chrono::milliseconds PollInterval = std::chrono::milliseconds(-1);

    bool HasErrorValue;
    uint64_t ErrorValue;
    EWordOrder WordOrder;
    uint8_t BitOffset;
    uint8_t BitWidth;
};
