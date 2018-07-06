#include "types.h"

using namespace std;

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

TValueSize RegisterFormatMaxSize(ERegisterFormat format)
{
    switch (format) {
        #define XX(name, alias, size) case name: return size;
        REGISTER_FORMATS
        #undef XX
        default: return 2;
    }
}

TValueSize RegisterFormatMaxWidth(ERegisterFormat format)
{
    return RegisterFormatMaxSize(format) * 8;
}
