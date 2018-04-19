#pragma once

#include "declarations.h"
#include "types.h"

#include <map>

// struct TRegisterType {
//     TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
//                   ERegisterFormat defaultFormat = U16,
//                   bool read_only = false, EWordOrder defaultWordOrder = EWordOrder::BigEndian):
//         Index(index), Name(name), DefaultControlType(defaultControlType),
//         DefaultFormat(defaultFormat), DefaultWordOrder(defaultWordOrder), ReadOnly(read_only) {}
//     int Index;
//     std::string Name, DefaultControlType;
//     ERegisterFormat DefaultFormat;
//     EWordOrder DefaultWordOrder;
//     bool ReadOnly;
// };

struct TMemoryBlockType {
    enum : uint16_t { Variadic = uint16_t(-1) };

    TMemoryBlockType(int index, const std::string & name, const std::string & defaultControlType,
                     uint16_t size, ERegisterFormat defaultFormat = U16, bool readOnly = false,
                     EByteOrder byteOrder = EByteOrder::BigEndian, EWordOrder defaultWordOrder = EWordOrder::BigEndian
    )
        : Index(index)
        , Name(name)
        , Defaults{defaultControlType, defaultFormat, defaultWordOrder}
        , Size(size)
        , ByteOrder(byteOrder)
        , ReadOnly(readOnly)
    {}

    int         Index;
    std::string Name;
    uint16_t    Size;
    EByteOrder  ByteOrder;
    bool        ReadOnly;

    bool IsVariadicSize() const
    {
        return Size == Variadic;
    }

    struct {
        std::string     ControlType;
        ERegisterFormat Format;
        EWordOrder      WordOrder;
    } Defaults;
};

using TRegisterType = TMemoryBlockType;

using TRegisterTypeMap  = std::map<std::string, TMemoryBlockType>;
using PRegisterTypeMap  = std::shared_ptr<TRegisterTypeMap>;

const char* RegisterFormatName(ERegisterFormat fmt);
ERegisterFormat RegisterFormatFromName(const std::string& name);
EWordOrder WordOrderFromName(const std::string& name);

struct TRegisterConfig
{
    TRegisterConfig(int type, int address,
                    ERegisterFormat format, double scale, double offset,
                    double round_to, bool poll, bool readonly,
                    const std::string& type_name,
                    bool has_error_value, uint64_t error_value,
                    const EWordOrder word_order, uint16_t bit_offset, uint8_t bit_width);

    static PRegisterConfig Create(int type = 0, int address = 0,
                                  ERegisterFormat format = U16, double scale = 1, double offset = 0,
                                  double round_to = 0, bool poll = true, bool readonly = false,
                                  const std::string& type_name = "",
                                  bool has_error_value = false,
                                  uint64_t error_value = 0,
                                  const EWordOrder word_order = EWordOrder::BigEndian,
                                  uint16_t bit_offset = 0, uint8_t bit_width = 0)
    {
        return std::make_shared<TRegisterConfig>(type, address, format, scale, offset, round_to, poll, readonly,
                                            type_name, has_error_value, error_value, word_order, bit_offset, bit_width);
    }

    uint8_t GetBitWidth() const;
    uint8_t GetFormatByteWidth() const;
    uint8_t GetFormatBitWidth() const;
    uint8_t GetFormatWordWidth() const;

    std::string ToString() const;
    std::string ToStringWithFormat() const;

    int Type;
    int Address;
    ERegisterFormat Format;
    double Scale;
    double Offset;
    double RoundTo;
    bool Poll;
    bool ReadOnly;
    std::string OnValue;
    std::string TypeName;
    std::chrono::milliseconds PollInterval = std::chrono::milliseconds(-1);

    bool        HasErrorValue;
    uint64_t    ErrorValue;
    EWordOrder  WordOrder;
    uint16_t    BitOffset;
    uint8_t     BitWidth;
};

inline ::std::ostream& operator<<(::std::ostream& os, PRegisterConfig reg) {
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TRegisterConfig& reg) {
    return os << reg.ToString();
}
