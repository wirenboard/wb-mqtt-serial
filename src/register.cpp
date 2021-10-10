#include "register.h"
#include "serial_device.h"
#include "bcd_utils.h"
#include <wblib/utils.h>
#include <string.h>
#include <string>

size_t RegisterFormatByteWidth(RegisterFormat format)
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

TRegisterRange::TRegisterRange(const std::list<PRegister>& regs): RegList(regs)
{
    if (RegList.empty())
        throw std::runtime_error("cannot construct empty register range");
    PRegister first = regs.front();
    RegDevice = first->Device();
    RegType = first->Type;
    RegTypeName = first->TypeName;
    RegPollInterval = first->PollInterval;
}

TRegisterRange::TRegisterRange(PRegister reg): RegList(1, reg)
{
    RegDevice = reg->Device();
    RegType = reg->Type;
    RegTypeName = reg->TypeName;
    RegPollInterval = reg->PollInterval;
}

const std::list<PRegister>& TRegisterRange::RegisterList() const
{
    return RegList;
}

std::list<PRegister>& TRegisterRange::RegisterList()
{
    return RegList;
}

PSerialDevice TRegisterRange::Device() const
{
    return RegDevice.lock();
}

int TRegisterRange::Type() const
{
    return RegType;
}

std::string TRegisterRange::TypeName() const
{
    return RegTypeName;
}

std::chrono::milliseconds TRegisterRange::PollInterval() const
{
    return RegPollInterval;
}

void TRegisterRange::SetError(EStatus error)
{
    for (auto& r: RegList) {
        r->SetError(error);
    }
}

TSimpleRegisterRange::TSimpleRegisterRange(const std::list<PRegister>& regs): TRegisterRange(regs)
{}

TSimpleRegisterRange::TSimpleRegisterRange(PRegister reg): TRegisterRange(reg)
{}

EStatus TSimpleRegisterRange::GetStatus() const
{
    bool hasOk = false;
    bool hasError = false;
    for (const auto& r: RegisterList()) {
        switch (r->GetError()) {
            case ST_DEVICE_ERROR:
                return ST_DEVICE_ERROR;
            case ST_OK: {
                hasOk = true;
                break;
            }
            case ST_UNKNOWN_ERROR:
                hasError = true;
                break;
        }
    }
    if (!hasError) {
        return ST_OK;
    }
    if (!hasOk) {
        return ST_UNKNOWN_ERROR;
    }
    return ST_DEVICE_ERROR;
}

std::string TRegisterConfig::ToString() const
{
    std::stringstream s;
    s << TypeName << ": " << GetAddress();
    if (BitOffset != 0 || BitWidth != 0) {
        s << ":" << (int)BitOffset << ":" << (int)BitWidth;
    }
    return s.str();
}

const IRegisterAddress& TRegisterConfig::GetAddress() const
{
    return *Address;
}

std::string TRegister::ToString() const
{
    if (Device()) {
        return "<" + Device()->ToString() + ":" + TRegisterConfig::ToString() + ">";
    }
    return "<unknown device:" + TRegisterConfig::ToString() + ">";
}

bool TRegister::IsAvailable() const
{
    return Available;
}

void TRegister::SetAvailable(bool available)
{
    Available = available;
}

EStatus TRegister::GetError() const
{
    return Error;
}

void TRegister::SetError(EStatus error)
{
    Error = error;
}

uint64_t TRegister::GetValue() const
{
    return Value;
}

void TRegister::SetValue(uint64_t value)
{
    Value = value;
    Error = ST_OK;
    Available = true;
}

const std::string& TRegister::GetChannelName() const
{
    return ChannelName;
}

std::map<std::tuple<PSerialDevice, PRegisterConfig>, PRegister> TRegister::RegStorage;
std::mutex TRegister::Mutex;

TRegisterConfig::TRegisterConfig(int type,
                                 std::shared_ptr<IRegisterAddress> address,
                                 RegisterFormat format,
                                 double scale,
                                 double offset,
                                 double round_to,
                                 bool poll,
                                 bool readonly,
                                 const std::string& type_name,
                                 std::unique_ptr<uint64_t> error_value,
                                 const EWordOrder word_order,
                                 uint8_t bit_offset,
                                 uint8_t bit_width,
                                 std::unique_ptr<uint64_t> unsupported_value)
    : Address(address),
      Type(type),
      Format(format),
      Scale(scale),
      Offset(offset),
      RoundTo(round_to),
      Poll(poll),
      ReadOnly(readonly),
      TypeName(type_name),
      ErrorValue(std::move(error_value)),
      WordOrder(word_order),
      BitOffset(bit_offset),
      BitWidth(bit_width),
      UnsupportedValue(std::move(unsupported_value))
{
    if (TypeName.empty())
        TypeName = "(type " + std::to_string(Type) + ")";

    auto maxOffset = RegisterFormatByteWidth(Format) * 8;

    if (BitOffset >= maxOffset) {
        throw TSerialDeviceException("bit offset must not exceed " + std::to_string(maxOffset) + " bits");
    }

    if (!Address) {
        throw TSerialDeviceException("register address is not defined");
    }
}

TRegisterConfig::TRegisterConfig(const TRegisterConfig& config)
{
    Type = config.Type;
    Address = config.Address;
    Format = config.Format;
    Scale = config.Scale;
    Offset = config.Offset;
    RoundTo = config.RoundTo;
    Poll = config.Poll;
    ReadOnly = config.ReadOnly;
    TypeName = config.TypeName;
    PollInterval = config.PollInterval;
    if (config.ErrorValue) {
        ErrorValue = std::make_unique<uint64_t>(*config.ErrorValue);
    }
    WordOrder = config.WordOrder;
    BitOffset = config.BitOffset;
    BitWidth = config.BitWidth;
    if (config.UnsupportedValue) {
        UnsupportedValue = std::make_unique<uint64_t>(*config.UnsupportedValue);
    }
}

uint8_t TRegisterConfig::GetByteWidth() const
{
    return RegisterFormatByteWidth(Format);
}

uint8_t TRegisterConfig::Get16BitWidth() const
{
    auto w = uint8_t(ceil(((float)BitOffset + GetBitWidth()) / 8) + 1) / 2;

    return w;
}

uint8_t TRegisterConfig::GetBitWidth() const
{
    if (BitWidth) {
        return BitWidth;
    }

    return GetByteWidth() * 8;
}

PRegisterConfig TRegisterConfig::Create(int type,
                                        std::shared_ptr<IRegisterAddress> address,
                                        RegisterFormat format,
                                        double scale,
                                        double offset,
                                        double round_to,
                                        bool poll,
                                        bool readonly,
                                        const std::string& type_name,
                                        std::unique_ptr<uint64_t> error_value,
                                        const EWordOrder word_order,
                                        uint8_t bit_offset,
                                        uint8_t bit_width,
                                        std::unique_ptr<uint64_t> unsupported_value)
{
    return std::make_shared<TRegisterConfig>(type,
                                             address,
                                             format,
                                             scale,
                                             offset,
                                             round_to,
                                             poll,
                                             readonly,
                                             type_name,
                                             std::move(error_value),
                                             word_order,
                                             bit_offset,
                                             bit_width,
                                             std::move(unsupported_value));
}

PRegisterConfig TRegisterConfig::Create(int type,
                                        uint32_t address,
                                        RegisterFormat format,
                                        double scale,
                                        double offset,
                                        double round_to,
                                        bool poll,
                                        bool readonly,
                                        const std::string& type_name,
                                        std::unique_ptr<uint64_t> error_value,
                                        const EWordOrder word_order,
                                        uint8_t bit_offset,
                                        uint8_t bit_width,
                                        std::unique_ptr<uint64_t> unsupported_value)
{
    return Create(type,
                  std::make_shared<TUint32RegisterAddress>(address),
                  format,
                  scale,
                  offset,
                  round_to,
                  poll,
                  readonly,
                  type_name,
                  std::move(error_value),
                  word_order,
                  bit_offset,
                  bit_width,
                  std::move(unsupported_value));
}

TUint32RegisterAddress::TUint32RegisterAddress(uint32_t address): Address(address)
{}

uint32_t TUint32RegisterAddress::Get() const
{
    return Address;
}

std::string TUint32RegisterAddress::ToString() const
{
    return std::to_string(Address);
}

bool TUint32RegisterAddress::operator<(const IRegisterAddress& addr) const
{
    auto a = dynamic_cast<const TUint32RegisterAddress&>(addr);
    return Address < a.Address;
}

IRegisterAddress* TUint32RegisterAddress::CalcNewAddress(uint32_t offset,
                                                         uint32_t stride,
                                                         uint32_t registerByteWidth,
                                                         uint32_t addressByteStep) const
{
    auto stride_offset = stride * registerByteWidth;
    if (addressByteStep < 1) {
        addressByteStep = 1;
    }
    stride_offset = (stride_offset + addressByteStep - 1) / addressByteStep;
    return new TUint32RegisterAddress(Address + offset + stride_offset);
}

uint32_t GetUint32RegisterAddress(const IRegisterAddress& addr)
{
    return dynamic_cast<const TUint32RegisterAddress&>(addr).Get();
}

TStringRegisterAddress::TStringRegisterAddress(const std::string& addr): Addr(addr)
{}

std::string TStringRegisterAddress::ToString() const
{
    return Addr;
}

bool TStringRegisterAddress::operator<(const IRegisterAddress& addr) const
{
    const auto& a = dynamic_cast<const TStringRegisterAddress&>(addr);
    return Addr < a.Addr;
}

IRegisterAddress* TStringRegisterAddress::CalcNewAddress(uint32_t /*offset*/,
                                                         uint32_t /*stride*/,
                                                         uint32_t /*registerByteWidth*/,
                                                         uint32_t /*addressByteStep*/) const
{
    return new TStringRegisterAddress(Addr);
}

TRegisterTypeMap::TRegisterTypeMap(const TRegisterTypes& types)
{
    if (types.empty()) {
        throw std::runtime_error("Register types are not defined");
    }
    for (const auto& rt: types) {
        RegTypes.insert(std::make_pair(rt.Name, rt));
    }
    DefaultType = types.front();
}

const TRegisterType& TRegisterTypeMap::Find(const std::string& typeName) const
{
    return RegTypes.at(typeName);
}

const TRegisterType& TRegisterTypeMap::GetDefaultType() const
{
    return DefaultType;
}

TRegisterType::TRegisterType(int index,
                             const std::string& name,
                             const std::string& defaultControlType,
                             RegisterFormat defaultFormat,
                             bool readOnly,
                             EWordOrder defaultWordOrder)
    : Index(index),
      Name(name),
      DefaultControlType(defaultControlType),
      DefaultFormat(defaultFormat),
      DefaultWordOrder(defaultWordOrder),
      ReadOnly(readOnly)
{}

template<typename T> T RoundValue(T val, double round_to)
{
    static_assert(std::is_floating_point<T>::value, "RoundValue accepts only floating point types");
    return round_to > 0 ? std::round(val / round_to) * round_to : val;
}

uint64_t InvertWordOrderIfNeeded(const TRegisterConfig& reg, uint64_t value)
{
    if (reg.WordOrder == EWordOrder::BigEndian) {
        return value;
    }

    uint64_t result = 0;
    uint64_t cur_value = value;

    for (int i = 0; i < reg.Get16BitWidth(); ++i) {
        uint16_t last_word = (((uint64_t)cur_value) & 0xFFFF);
        result <<= 16;
        result |= last_word;
        cur_value >>= 16;
    }
    return result;
}

template<class T> struct TConvertTraits
{};

template<> struct TConvertTraits<int64_t>
{
    static int64_t FromScaledTextValue(const TRegisterConfig& reg, const std::string& str, int base)
    {
        size_t pos;
        auto value = std::stoll(str.c_str(), &pos, base);
        if (pos == str.size()) {
            if (reg.Scale == 1 && reg.Offset == 0) {
                return value;
            }
            return llround((value - reg.Offset) / reg.Scale);
        }
        throw std::invalid_argument("\"" + str + "\" can't be converted to integer");
    }
};

template<> struct TConvertTraits<uint64_t>
{
    static uint64_t FromScaledTextValue(const TRegisterConfig& reg, const std::string& str, int base)
    {
        size_t pos;
        auto value = std::stoull(str.c_str(), &pos, base);
        if (pos == str.size()) {
            if (reg.Scale == 1 && reg.Offset == 0) {
                return value;
            }
            auto res = llround((value - reg.Offset) / reg.Scale);
            if (res < 0) {
                throw std::out_of_range("\"" + str + "\" after applying scale and offset is not an unsigned integer: " +
                                        std::to_string(res));
            }
            return res;
        }
        throw std::invalid_argument("\"" + str + "\" can't be converted to unsigned integer");
    }
};

template<typename T> T FromScaledTextValue(const TRegisterConfig& reg, const std::string& str)
{
    if (str.empty()) {
        throw std::invalid_argument("empty string can't be converted to number");
    }
    if (WBMQTT::StringStartsWith(str, "0x") || WBMQTT::StringStartsWith(str, "0X")) {
        return TConvertTraits<T>::FromScaledTextValue(reg, str, 16);
    }
    try {
        return TConvertTraits<T>::FromScaledTextValue(reg, str, 10);
    } catch (const std::invalid_argument&) {
        auto res = llround(FromScaledTextValue<double>(reg, str));
        if (std::is_unsigned<T>::value && (res < 0)) {
            throw std::out_of_range(
                "\"" + str + "\" after applying scale and offset is not an unsigned integer: " + std::to_string(res));
        }
        return res;
    }
}

template<> double FromScaledTextValue(const TRegisterConfig& reg, const std::string& str)
{
    if (!str.empty()) {
        size_t pos;
        double resd = std::stod(str.c_str(), &pos);
        if (pos == str.size()) {
            return (RoundValue(resd, reg.RoundTo) - reg.Offset) / reg.Scale;
        }
    }
    throw std::invalid_argument("");
}

uint64_t GetRawValue(const TRegisterConfig& reg, const std::string& str)
{
    switch (reg.Format) {
        case S8:
            return FromScaledTextValue<int64_t>(reg, str) & 0xff;
        case S16:
            return FromScaledTextValue<int64_t>(reg, str) & 0xffff;
        case S24:
            return FromScaledTextValue<int64_t>(reg, str) & 0xffffff;
        case S32:
            return FromScaledTextValue<int64_t>(reg, str) & 0xffffffff;
        case S64:
            return FromScaledTextValue<int64_t>(reg, str);
        case U8:
            return FromScaledTextValue<uint64_t>(reg, str) & 0xff;
        case U16:
            return FromScaledTextValue<uint64_t>(reg, str) & 0xffff;
        case U24:
            return FromScaledTextValue<uint64_t>(reg, str) & 0xffffff;
        case U32:
            return FromScaledTextValue<uint64_t>(reg, str) & 0xffffffff;
        case U64:
            return FromScaledTextValue<uint64_t>(reg, str);
        case Float: {
            float v = FromScaledTextValue<double>(reg, str);
            uint64_t raw = 0;
            memcpy(&raw, &v, sizeof(v));
            return raw;
        }
        case Double: {
            double v = FromScaledTextValue<double>(reg, str);
            uint64_t raw = 0;
            memcpy(&raw, &v, sizeof(v));
            return raw;
        }
        case Char8:
            return str.empty() ? 0 : uint8_t(str[0]);
        case BCD8:
            return IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFF, WordSizes::W8_SZ);
        case BCD16:
            return IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFF, WordSizes::W16_SZ);
        case BCD24:
            return IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFFFF, WordSizes::W24_SZ);
        case BCD32:
            return IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFFFFFF, WordSizes::W32_SZ);
        default:
            return FromScaledTextValue<uint64_t>(reg, str);
    }
}

uint64_t ConvertToRawValue(const TRegisterConfig& reg, const std::string& str)
{
    return InvertWordOrderIfNeeded(reg, GetRawValue(reg, str));
}

template<typename T> std::string ToScaledTextValue(const TRegisterConfig& reg, T val)
{
    if (reg.Scale == 1 && reg.Offset == 0 && reg.RoundTo == 0) {
        return std::to_string(val);
    }
    // potential loss of precision
    return ToScaledTextValue<double>(reg, val);
}

template<> std::string ToScaledTextValue(const TRegisterConfig& reg, float val)
{
    return WBMQTT::StringFormat("%.7g", RoundValue(reg.Scale * val + reg.Offset, reg.RoundTo));
}

template<> std::string ToScaledTextValue(const TRegisterConfig& reg, double val)
{
    return WBMQTT::StringFormat("%.15g", RoundValue(reg.Scale * val + reg.Offset, reg.RoundTo));
}

std::string ConvertFromRawValue(const TRegisterConfig& reg, uint64_t value)
{
    value = InvertWordOrderIfNeeded(reg, value);
    switch (reg.Format) {
        case S8:
            return ToScaledTextValue(reg, int8_t(value & 0xff));
        case S16:
            return ToScaledTextValue(reg, int16_t(value & 0xffff));
        case S24: {
            uint32_t v = value & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return ToScaledTextValue(reg, int32_t(v));
        }
        case S32:
            return ToScaledTextValue(reg, int32_t(value & 0xffffffff));
        case S64:
            return ToScaledTextValue(reg, int64_t(value));
        case BCD8:
            return ToScaledTextValue(reg, PackedBCD2Int(value, WordSizes::W8_SZ));
        case BCD16:
            return ToScaledTextValue(reg, PackedBCD2Int(value, WordSizes::W16_SZ));
        case BCD24:
            return ToScaledTextValue(reg, PackedBCD2Int(value, WordSizes::W24_SZ));
        case BCD32:
            return ToScaledTextValue(reg, PackedBCD2Int(value, WordSizes::W32_SZ));
        case Float: {
            float v;
            memcpy(&v, &value, sizeof(v));
            return ToScaledTextValue(reg, v);
        }
        case Double: {
            double v;
            memcpy(&v, &value, sizeof(v));
            return ToScaledTextValue(reg, v);
        }
        case Char8:
            return std::string(1, value & 0xff);
        default:
            return ToScaledTextValue(reg, value);
    }
}
