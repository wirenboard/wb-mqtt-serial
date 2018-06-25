#include "register_config.h"
#include "serial_exc.h"

#include <sstream>
#include <cassert>

using namespace std;


ERegisterFormat TMemoryBlockType::GetDefaultFormat(uint16_t bit) const
{
    if (Layout.empty()) {
        throw TSerialDeviceException("unable to get default format from memory block type '" + Name + "': none specified");
    }

    uint16_t pos = 0;
    for (auto format: Layout) {
        pos += RegisterFormatMaxSize(format) * 8;
        if (pos > bit) {
            return format;
        }
    }

    throw TSerialDeviceException("unable to get default format from memory block type '" + Name + "' for bit " + to_string(bit));
}

uint16_t TMemoryBlockType::GetValueCount() const
{
    return Layout.empty() ? 1 : Layout.size();
}

uint16_t TMemoryBlockType::GetValueByteIndex(uint16_t iValue) const
{
    uint16_t iByte = 0;
    for (uint16_t i = 0; i < NormalizeValueIndex(iValue); ++i) {
        iByte += GetValueSize(NormalizeValueIndex(i));
    }

    return iByte;
}

uint8_t TMemoryBlockType::GetValueSize(uint16_t iValue) const
{
    assert(!Layout.empty());
    return RegisterFormatMaxSize(Layout[iValue]);
}

std::pair<uint16_t, uint8_t> TMemoryBlockType::ToMaskParameters(uint16_t iValue) const
{
    auto iByte = GetValueByteIndex(iValue);
    auto valueSize = GetValueSize(iValue);

    // mask treats shifts opposite to value bytes
    iByte = Size - iByte - valueSize;

    std::pair<uint16_t, uint8_t> res { iByte * 8, valueSize * 8 };

    return res;
}

uint16_t TMemoryBlockType::NormalizeValueIndex(uint16_t iValue) const
{
    auto valueCount = GetValueCount();
    assert(iValue < valueCount);
    iValue = ByteOrder == EByteOrder::BigEndian ? iValue : valueCount - iValue - 1;
    assert(iValue < valueCount);
    return iValue;
}

const char* RegisterFormatName(ERegisterFormat fmt) {
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
    case Char8:
        return "Char8";
    default:
        return "<unknown register type>";
    }
}

ERegisterFormat RegisterFormatFromName(const std::string& name) {
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
    else if (name == "char8")
        return Char8;
    else
        return U16; // FIXME!
}

EWordOrder WordOrderFromName(const std::string& name) {
    if (name == "big_endian")
        return EWordOrder::BigEndian;
    else if (name == "little_endian")
        return EWordOrder::LittleEndian;
    else
        return EWordOrder::BigEndian;
}


TRegisterConfig::TRegisterConfig(int type, int address,
            ERegisterFormat format, double scale, double offset,
            double round_to, bool poll, bool readonly,
            const std::string& type_name,
            bool has_error_value, uint64_t error_value,
            const EWordOrder word_order, uint16_t bit_offset, uint8_t width)
    : Type(type), Address(address), Format(format)
    , Scale(scale), Offset(offset), RoundTo(round_to)
    , Poll(poll), ReadOnly(readonly), TypeName(type_name)
    , HasErrorValue(has_error_value), ErrorValue(error_value)
    , WordOrder(word_order), BitOffset(bit_offset), Width(width)
{
    if (TypeName.empty())
        TypeName = "(type " + std::to_string(Type) + ")";
}

TValueSize TRegisterConfig::GetWidth() const {
    if (Width) {
        return Width;
    }

    return GetFormatMaxWidth();
}

TValueSize TRegisterConfig::GetFormatMaxSize() const {
    return RegisterFormatMaxSize(Format);
}

TValueSize TRegisterConfig::GetFormatMaxWidth() const {
    return GetFormatMaxSize() * 8;
}

string TRegisterConfig::ToString() const
{
    stringstream s;
    s << TypeName << ": " << Address;
    if (BitOffset || Width) {
        s << ":" << (int)BitOffset << ":" << (int)Width;
    }

    return s.str();
}

string TRegisterConfig::ToStringWithFormat() const
{
    return ToString() + " (" + RegisterFormatName(Format) + ")";
}
