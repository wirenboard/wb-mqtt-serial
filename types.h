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

enum class EQueryStatus {
    OK,
    UNKNOWN_ERROR,            // response from device either not parsed or not received at all (crc error, timeout)
    DEVICE_TRANSIENT_ERROR,   // valid response from device, which reports error that can disappear over time by itself
    DEVICE_PERMANENT_ERROR    // valid response from device, which reports error that cannot disappear by itself and driver needs to take actions in order to eliminate this error
};

enum class EQueryOperation: uint8_t {
    READ = 0,
    WRITE
};

enum class EErrorState: uint8_t {
    NoError             = 0x0,
    WriteError          = 0x1,
    ReadError           = 0x2,
    ReadWriteError      = ReadError | WriteError,
    UnknownErrorState   = 255
};

bool operator&(EErrorState, EErrorState);

std::ostream& operator<<(::std::ostream& os, EWordOrder val);

uint8_t RegisterFormatByteWidth(ERegisterFormat format);
