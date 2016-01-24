#pragma once
#include <map>
#include <vector>
#include <memory>
#include <sstream>
#include <functional>

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

struct TRegisterType {
    TRegisterType(int index, const std::string& name, const std::string& defaultControlType,
                  RegisterFormat defaultFormat = U16, bool read_only = false):
        Index(index), Name(name), DefaultControlType(defaultControlType),
        DefaultFormat(defaultFormat), ReadOnly(read_only) {}
    int Index;
    std::string Name, DefaultControlType;
    RegisterFormat DefaultFormat;
    bool ReadOnly;
};

typedef std::vector<TRegisterType> TRegisterTypes;
typedef std::map<std::string, TRegisterType> TRegisterTypeMap;
typedef std::shared_ptr<TRegisterTypeMap> PRegisterTypeMap;

struct TRegister
{
    TRegister(int slave = 0, int type = 0, int address = 0,
              RegisterFormat format = U16, double scale = 1,
              bool poll = true, bool readonly = false,
              const std::string& type_name = "")
        : Slave(slave), Type(type), Address(address), Format(format),
          Scale(scale), Poll(poll), ReadOnly(readonly), TypeName(type_name)
    {
        if (TypeName.empty())
            TypeName = "(type " + std::to_string(Type) + ")";
    }

    uint8_t ByteWidth() const {
        switch (Format) {
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
            default:
                return 2;
        }
    }

    uint8_t Width() const {
        return (ByteWidth() + 1) / 2;
    }

    std::string ToString() const {
        std::stringstream s;
        s << "<" << Slave << ":" << TypeName << ": " << Address << ">";
        return s.str();
    }

    bool operator< (const TRegister& reg) const {
        if (Slave < reg.Slave)
            return true;
        if (Slave > reg.Slave)
            return false;
        if (Type < reg.Type)
            return true;
        if (Type > reg.Type)
            return false;
        return Address < reg.Address;
    }

    bool operator== (const TRegister& reg) const {
        return Slave == reg.Slave && Type == reg.Type && Address == reg.Address;
    }

    int Slave;
    int Type;
    int Address;
    RegisterFormat Format;
    double Scale;
    bool Poll;
    bool ReadOnly;
    std::string TypeName;
};

typedef std::shared_ptr<TRegister> PRegister;

inline ::std::ostream& operator<<(::std::ostream& os, PRegister reg) {
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TRegister& reg) {
    return os << reg.ToString();
}

namespace std {
    template <> struct hash<PRegister> {
        size_t operator()(PRegister p) const {
            return hash<int>()(p->Slave) ^ hash<int>()(p->Type) ^ hash<int>()(p->Address);
        }
    };
}

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

inline RegisterFormat RegisterFormatFromName(const std::string& name) {
    if (name == "s16")
        return S16;
    else if (name == "u8")
        return U8;
    else if (name == "s8")
        return S8;
    else if (name == "u24")
        return U24;
    else if (name == "s24")
        return S24;
    else if (name == "u32")
        return U32;
    else if (name == "s32")
        return S32;
    else if (name == "s64")
        return S64;
    else if (name == "u64")
        return U64;
    else if (name == "bcd8")
        return BCD8;
    else if (name == "bcd16")
        return BCD16;
    else if (name == "bcd24")
        return BCD24;
    else if (name == "bcd32")
        return BCD32;
    else if (name == "float")
        return Float;
    else if (name == "double")
        return Double;
    else
        return U16; // FIXME!
}
