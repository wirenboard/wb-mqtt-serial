#include "register_config.h"

#include <sstream>

using namespace std;


TRegisterConfig::TRegisterConfig(int type, int address,
            RegisterFormat format, double scale, double offset,
            double round_to, bool poll, bool readonly,
            const std::string& type_name,
            bool has_error_value, uint64_t error_value,
            const EWordOrder word_order, uint8_t bit_offset, uint8_t bit_width)
    : Type(type), Address(address), Format(format)
    , Scale(scale), Offset(offset), RoundTo(round_to)
    , Poll(poll), ReadOnly(readonly), TypeName(type_name)
    , HasErrorValue(has_error_value), ErrorValue(error_value)
    , WordOrder(word_order), BitOffset(bit_offset), BitWidth(bit_width)
{
    if (TypeName.empty())
        TypeName = "(type " + std::to_string(Type) + ")";
}

PRegisterConfig TRegisterConfig::Create(int type = 0, int address = 0,
                        RegisterFormat format = U16, double scale = 1, double offset = 0,
                        double round_to = 0, bool poll = true, bool readonly = false,
                        const std::string& type_name = "",
                        bool has_error_value = false,
                        uint64_t error_value = 0,
                        const EWordOrder word_order = EWordOrder::BigEndian,
                        uint8_t bit_offset = 0, uint8_t bit_width = 0
                        )
{
    return std::make_shared<TRegisterConfig>(type, address, format, scale, offset, round_to, poll, readonly,
                                        type_name, has_error_value, error_value, word_order, bit_offset, bit_width);
}

uint8_t TRegisterConfig::GetBitWidth() const {
    if (BitWidth) {
        return BitWidth;
    }

    return ByteWidth() * 8;
}

uint8_t TRegisterConfig::ByteWidth() const {
    return RegisterFormatByteWidth(Format);
}

uint8_t TRegisterConfig::Width() const {
    return (ByteWidth() + 1) / 2;
}

string TRegisterConfig::ToString() const
{
    stringstream s;
    s << TypeName << ": " << Address;
    return s.str();
}
