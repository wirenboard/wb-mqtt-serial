#pragma once

#include <ostream>

enum ERegisterFormat {
    AUTO,
    U8,
    S8,
    U16,
    S16,
    S24,
    U24,
    U32,
    S32,
    S64,
    U64,
    BCD8,
    BCD16,
    BCD24,
    BCD32,
    Float,
    Double,
    Char8
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

uint8_t RegisterFormatByteWidth(ERegisterFormat format);
