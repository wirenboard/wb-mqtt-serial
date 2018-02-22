#include "types.h"

using namespace std;

bool operator&(EErrorState lhs, EErrorState rhs)
{
    return static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs);
}

ostream& operator<<(ostream& os, EWordOrder val)
{
    switch (val) {
        case EWordOrder::BigEndian:
            return os << "BigEndian";
        case EWordOrder::LittleEndian:
            return os << "LittleEndian";
    }

    return os;
}

uint8_t RegisterFormatByteWidth(ERegisterFormat format)
{
    switch (format) {
        case S64:
        case U64:
        case Double:
            return 8;
        case U32:
        case S32:
        case BCD32:
        case Float:
            return 4;
        case U24:
        case S24:
        case BCD24:
            return 3;
        case Char8:
            return 1;
        default:
            return 2;
    }
}
