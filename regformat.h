#pragma once

#include <string>

enum RegisterFormat {
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
    Double
};

inline const char* RegisterFormatName(RegisterFormat fmt) {
    switch (fmt) {
    case AUTO:
        return "AUTO";
    case U8:
        return "U8";
    case S8:
        return "S8";
    case U16:
        return "U16";
    case S16:
        return "S16";
    case S24:
        return "S24";
    case U24:
        return "U24";
    case U32:
        return "U32";
    case S32:
        return "S32";
    case S64:
        return "S64";
    case U64:
        return "U64";
    case BCD8:
        return "BCD8";
    case BCD16:
        return "BCD16";
    case BCD24:
        return "BCD24";
    case BCD32:
        return "BCD32";
    case Float:
        return "Float";
    case Double:
        return "Double";
    default:
        return "<unknown register type>";
    }
}
