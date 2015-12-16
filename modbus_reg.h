#pragma once
#include <memory>
#include <sstream>

#include "regformat.h"

struct TModbusRegister
{
    enum RegisterType { COIL, DISCRETE_INPUT, HOLDING_REGISTER, INPUT_REGISTER, DIRECT_REGISTER };

    TModbusRegister(int slave = 0, RegisterType type = COIL, int address = 0,
                     RegisterFormat format = U16, double scale = 1,
                     bool poll = true, bool readonly = false)
        : Slave(slave), Type(type), Address(address), Format(format),
          Scale(scale), Poll(poll), ForceReadOnly(readonly) {}

    int Slave;
    RegisterType Type;
    int Address;
    RegisterFormat Format;
    double Scale;
    bool Poll;
    bool ForceReadOnly;

    bool IsReadOnly() const {
        return Type == RegisterType::DISCRETE_INPUT ||
            Type == RegisterType::INPUT_REGISTER || ForceReadOnly;
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
        return ByteWidth() / 2;
    }

    std::string ToString() const {
        std::stringstream s;
        s << "<" << Slave << ":" <<
            (Type == COIL ? "coil" :
             Type == DISCRETE_INPUT ? "discrete" :
             Type == HOLDING_REGISTER ? "holding" :
             Type == INPUT_REGISTER ? "input" :
             Type == DIRECT_REGISTER ? "direct" :
             "bad") << ": " << Address << ">";
        return s.str();
    }

    bool operator< (const TModbusRegister& reg) const {
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

    bool operator== (const TModbusRegister& reg) const {
        return Slave == reg.Slave && Type == reg.Type && Address == reg.Address;
    }
};

inline ::std::ostream& operator<<(::std::ostream& os, std::shared_ptr<TModbusRegister> reg) {
    return os << reg->ToString();
}

inline ::std::ostream& operator<<(::std::ostream& os, const TModbusRegister& reg) {
    return os << reg.ToString();
}

namespace std {
    template <> struct hash<std::shared_ptr<TModbusRegister>> {
        size_t operator()(std::shared_ptr<TModbusRegister> p) const {
            return hash<int>()(p->Slave) ^ hash<int>()(p->Type) ^ hash<int>()(p->Address);
        }
    };
}
