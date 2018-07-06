#pragma once

#include "constraints.h"

#include <ostream>

#define REGISTER_FORMATS \
    XX(AUTO, "auto", 0) \
    XX(U8, "u8", 1) \
    XX(S8, "s8", 1) \
    XX(U16, "u16", 2) \
    XX(S16, "s16", 2) \
    XX(S24, "s24", 3) \
    XX(U24, "u24", 3) \
    XX(U32, "u32", 4) \
    XX(S32, "s32", 4) \
    XX(S64, "s64", 8) \
    XX(U64, "u64", 8) \
    XX(BCD8, "bcd8", 1) \
    XX(RBCD8, "rbcd8", 1) \
    XX(BCD16, "bcd16", 2) \
    XX(RBCD16, "rbcd16", 2) \
    XX(BCD24, "bcd24", 3) \
    XX(RBCD24, "rbcd24", 3) \
    XX(BCD32, "bcd32", 4) \
    XX(RBCD32, "rbcd32", 4) \
    XX(Float, "float", 4) \
    XX(Double, "double", 8) \
    XX(Char8, "char8", 1) \
    XX(String, "string", MAX_STRING_VALUE_SIZE) \
    XX(WString, "wstring", MAX_STRING_VALUE_SIZE)

enum ERegisterFormat {
    #define XX(name, alias, size) name,
    REGISTER_FORMATS
    #undef XX
};

enum class EWordOrder {
    BigEndian,
    LittleEndian
};

using EByteOrder = EWordOrder;

enum class EQueryStatus {
    NotExecuted,            // query wasn't yet executed in this cycle
    Ok,                     // exec is ok
    UnknownError,           // response from device not received at all (timeout)
    DeviceTransientError,   // valid or invalid response from device, which reports error that can disappear over time by itself (crc error)
    DevicePermanentError    // valid response from device, which reports error that cannot disappear by itself and driver needs to take actions in order to eliminate this error
};

enum class EQueryOperation: uint8_t {
    Read = 0,
    Write
};

enum class EErrorState: uint8_t {
    NoError             = 0x0,
    WriteError          = 0x1 << 0,
    ReadError           = 0x1 << 1,
    ReadWriteError      = ReadError | WriteError,
    UnknownErrorState   = 0x1 << 2
};

enum class EPublishData: uint8_t {
    None            = 0x0,              // no publish needed
    Value           = 0x1 << 0,         // only value needs to publish
    Error           = 0x1 << 1,         // only error needs to publish
    ValueAndError   = Value | Error     // both error and value needs to publish
};

template <typename T>
bool Has(T a, T b)
{
    static_assert(std::is_enum<T>::value, "'Has' ment to be used with enums");

    return static_cast<uint64_t>(a) & static_cast<uint64_t>(b);
}

template <typename T>
void Add(T & a, T b)
{
    static_assert(std::is_enum<T>::value, "'Add' ment to be used with enums");

    a = static_cast<T>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

template <typename T>
void Remove(T & a, T b)
{
    static_assert(std::is_enum<T>::value, "'Add' ment to be used with enums");

    a = static_cast<T>(static_cast<uint64_t>(a) & ~static_cast<uint64_t>(b));
}

std::ostream& operator<<(::std::ostream& os, EWordOrder val);

TValueSize RegisterFormatMaxSize(ERegisterFormat format);
TValueSize RegisterFormatMaxWidth(ERegisterFormat format);
