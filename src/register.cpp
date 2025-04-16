#include "register.h"
#include "bcd_utils.h"
#include "serial_device.h"
#include <string.h>
#include <string>
#include <wblib/utils.h>

#include "log.h"

#define LOG(logger) ::logger.Log() << "[register] "

size_t RegisterFormatByteWidth(RegisterFormat format)
{
    switch (format) {
        case String:
        case String8:
            return 0; // The size will then be taken from the config parameter
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
        case U16:
        case S16:
        case BCD16:
            return 2;
        case U8:
        case S8:
        case BCD8:
        case Char8:
            return 1;
    }
    return 2;
}

const std::list<PRegister>& TRegisterRange::RegisterList() const
{
    return RegList;
}

std::list<PRegister>& TRegisterRange::RegisterList()
{
    return RegList;
}

bool TRegisterRange::HasOtherDeviceAndType(PRegister reg) const
{
    if (RegisterList().empty()) {
        return false;
    }
    auto& frontReg = RegisterList().front();
    return ((reg->Device() != frontReg->Device()) || (reg->Type != frontReg->Type));
}

bool TSameAddressRegisterRange::Add(PRegister reg, std::chrono::milliseconds pollLimit)
{
    if (HasOtherDeviceAndType(reg)) {
        return false;
    }
    if (RegisterList().empty() || reg->GetAddress().Compare(RegisterList().front()->GetAddress()) == 0) {
        RegisterList().push_back(reg);
        return true;
    }
    return false;
}

bool TRegisterConfig::IsString() const
{
    return Format == String || Format == String8;
}

std::string TRegisterConfig::ToString() const
{
    std::stringstream s;
    s << TypeName << ": " << (AccessType != EAccessType::WRITE_ONLY ? GetAddress() : GetWriteAddress());
    if (Address.DataOffset != 0 || Address.DataWidth != 0) {
        s << ":" << static_cast<int>(Address.DataOffset) << ":" << static_cast<int>(Address.DataWidth);
    }
    return s.str();
}

bool TRegisterConfig::IsHighPriority() const
{
    return bool(ReadPeriod);
}

const IRegisterAddress& TRegisterConfig::GetAddress() const
{
    if (AccessType == EAccessType::WRITE_ONLY) {
        throw TSerialDeviceException("Missing read address. Write-only register");
    }
    return *Address.Address;
}

const IRegisterAddress& TRegisterConfig::GetWriteAddress() const
{
    if (Address.WriteAddress != nullptr) {
        return *Address.WriteAddress;
    }
    return *Address.Address;
}

TRegister::TRegister(PSerialDevice device, PRegisterConfig config)
    : TRegisterConfig(*config),
      _Device(device),
      ReadPeriodMissChecker(config->ReadPeriod)
{}

std::string TRegister::ToString() const
{
    if (Device()) {
        return "<" + Device()->ToString() + ":" + TRegisterConfig::ToString() + ">";
    }
    return "<unknown device:" + TRegisterConfig::ToString() + ">";
}

TRegisterAvailability TRegister::GetAvailable() const
{
    return Available;
}

void TRegister::SetAvailable(TRegisterAvailability available)
{
    Available = available;
    if (Available == TRegisterAvailability::UNAVAILABLE) {
        ExcludeFromPolling();
    }
}

TRegisterValue TRegister::GetValue() const
{
    return Value;
}

void TRegister::SetValue(const TRegisterValue& value, bool clearReadError)
{
    if (::Debug.IsEnabled() && (Value != value)) {
        std::string formatName = RegisterFormatName(Format);
        if (IsString()) {
            LOG(Debug) << ToString() << " (" << formatName << ") new value: \"" << value << "\"";
        } else {
            LOG(Debug) << ToString() << " (" << formatName << ") new value: 0x" << std::setfill('0')
                       << std::setw(RegisterFormatByteWidth(Format) * 2) << std::hex << value;
        }
    }
    Value = value;
    if (UnsupportedValue && (*UnsupportedValue == value)) {
        SetError(TRegister::TError::ReadError);
        SetAvailable(TRegisterAvailability::UNAVAILABLE);
        LOG(Warn) << ToString() << " is now marked as unavailable: unsupported value received";
        return;
    }
    SetAvailable(TRegisterAvailability::AVAILABLE);
    if (ErrorValue && ErrorValue.value() == value) {
        LOG(Debug) << ToString() << " contains error value";
        SetError(TError::ReadError);
    } else {
        if (clearReadError) {
            ClearError(TError::ReadError);
        }
    }
}

void TRegister::SetError(TRegister::TError error)
{
    ErrorState.set(error);
}

void TRegister::ClearError(TRegister::TError error)
{
    ErrorState.reset(error);
}

const TRegister::TErrorState& TRegister::GetErrorState() const
{
    return ErrorState;
}

void TRegister::SetLastPollTime(std::chrono::steady_clock::time_point pollTime)
{
    if (!ReadPeriod) {
        return;
    }
    if (ReadPeriodMissChecker.IsMissed(pollTime)) {
        SetError(TError::PollIntervalMissError);
    } else {
        ClearError(TError::PollIntervalMissError);
    }
}

bool TRegister::IsExcludedFromPolling() const
{
    return ExcludedFromPolling;
}

void TRegister::ExcludeFromPolling()
{
    ExcludedFromPolling = true;
}

void TRegister::IncludeInPolling()
{
    ExcludedFromPolling = false;
}

TReadPeriodMissChecker::TReadPeriodMissChecker(const std::optional<std::chrono::milliseconds>& readPeriod)
    : TotalReadTime(std::chrono::milliseconds::zero()),
      ReadCount(0)
{
    if (readPeriod) {
        ReadMissCheckInterval = std::max(std::chrono::milliseconds(10000), (*readPeriod) * 10);
        MaxReadPeriod = std::chrono::duration_cast<std::chrono::milliseconds>((*readPeriod) * 1.1);
    }
}

bool TReadPeriodMissChecker::IsMissed(std::chrono::steady_clock::time_point readTime)
{
    bool res = false;
    if (LastReadTime.time_since_epoch().count()) {
        ++ReadCount;
        TotalReadTime += std::chrono::duration_cast<std::chrono::milliseconds>(readTime - LastReadTime);
        if (TotalReadTime >= ReadMissCheckInterval) {
            if (TotalReadTime / ReadCount >= MaxReadPeriod) {
                res = true;
            }
            ReadCount = 0;
            TotalReadTime = std::chrono::milliseconds::zero();
        }
    }
    LastReadTime = readTime;
    return res;
}

TRegisterConfig::TRegisterConfig(int type,
                                 const TRegisterDesc& registerAddressesDescription,
                                 RegisterFormat format,
                                 double scale,
                                 double offset,
                                 double round_to,
                                 TSporadicMode sporadic,
                                 bool readonly,
                                 const std::string& type_name,
                                 const EWordOrder word_order)
    : Address(registerAddressesDescription),
      Type(type),
      Format(format),
      Scale(scale),
      Offset(offset),
      RoundTo(round_to),
      SporadicMode(sporadic),
      TypeName(type_name),
      WordOrder(word_order)
{
    if (TypeName.empty())
        TypeName = "(type " + std::to_string(Type) + ")";

    auto maxOffset = RegisterFormatByteWidth(Format) * 8;

    if (!IsString() && Address.DataOffset >= maxOffset) {
        throw TSerialDeviceException("bit offset must not exceed " + std::to_string(maxOffset) + " bits");
    }

    if (!Address.Address && Address.WriteAddress) {
        AccessType = EAccessType::WRITE_ONLY;
    } else if (!Address.Address && !Address.WriteAddress) {
        throw TSerialDeviceException("write and read register address are not defined");
    }
    if (readonly) {
        if (AccessType == EAccessType::WRITE_ONLY) {
            throw TSerialDeviceException("Invalid attribute: readonly. Write-only register");
        } else {
            AccessType = EAccessType::READ_ONLY;
        }
    }
}

uint32_t TRegisterConfig::GetByteWidth() const
{
    return RegisterFormatByteWidth(Format);
}

uint8_t TRegisterConfig::Get16BitWidth() const
{
    if (IsString()) {
        return GetDataWidth() / (sizeof(char) * 8);
    }
    auto totalBit = std::max(GetByteWidth() * 8, Address.DataOffset + GetDataWidth());
    return totalBit / 16 + (totalBit % 16 ? 1 : 0);
}

uint32_t TRegisterConfig::GetDataWidth() const
{
    if (Address.DataWidth) {
        return Address.DataWidth;
    }
    return GetByteWidth() * 8;
}

uint32_t TRegisterConfig::GetDataOffset() const
{
    return Address.DataOffset;
}

void TRegisterConfig::SetDataWidth(uint8_t width)
{
    Address.DataWidth = width;
}
void TRegisterConfig::SetDataOffset(uint8_t offset)
{
    Address.DataOffset = offset;
}

PRegisterConfig TRegisterConfig::Create(int type,
                                        const TRegisterDesc& registerAddressesDescription,
                                        RegisterFormat format,
                                        double scale,
                                        double offset,
                                        double round_to,
                                        TSporadicMode sporadic,
                                        bool readonly,
                                        const std::string& type_name,
                                        const EWordOrder word_order)
{
    return std::make_shared<TRegisterConfig>(type,
                                             registerAddressesDescription,
                                             format,
                                             scale,
                                             offset,
                                             round_to,
                                             sporadic,
                                             readonly,
                                             type_name,
                                             word_order);
}

PRegisterConfig TRegisterConfig::Create(int type,
                                        uint32_t address,
                                        RegisterFormat format,
                                        double scale,
                                        double offset,
                                        double round_to,
                                        TSporadicMode sporadic,
                                        bool readonly,
                                        const std::string& type_name,
                                        const EWordOrder word_order,
                                        uint32_t data_offset,
                                        uint32_t data_bit_width)
{
    TRegisterDesc regAddressesDescription;
    regAddressesDescription.Address = std::make_shared<TUint32RegisterAddress>(address);
    regAddressesDescription.WriteAddress = regAddressesDescription.Address;
    regAddressesDescription.DataOffset = data_offset;
    regAddressesDescription.DataWidth = data_bit_width;

    return Create(type,
                  regAddressesDescription,
                  format,
                  scale,
                  offset,
                  round_to,
                  sporadic,
                  readonly,
                  type_name,
                  word_order);
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

int TUint32RegisterAddress::Compare(const IRegisterAddress& addr) const
{
    auto a = dynamic_cast<const TUint32RegisterAddress&>(addr);
    if (Address < a.Address) {
        return -1;
    }
    return (Address == a.Address) ? 0 : 1;
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

int TStringRegisterAddress::Compare(const IRegisterAddress& addr) const
{
    const auto& a = dynamic_cast<const TStringRegisterAddress&>(addr);
    return Addr.compare(a.Addr);
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

TRegisterValue GetRawValue(const TRegisterConfig& reg, const std::string& str)
{
    TRegisterValue value;
    switch (reg.Format) {
        case S8:
            value.Set(static_cast<uint64_t>(FromScaledTextValue<int64_t>(reg, str) & 0xff));
            break;
        case S16:
            value.Set(static_cast<uint64_t>(FromScaledTextValue<int64_t>(reg, str) & 0xffff));
            break;
        case S24:
            value.Set(static_cast<uint64_t>(FromScaledTextValue<int64_t>(reg, str) & 0xffffff));
            break;
        case S32:
            value.Set(static_cast<uint64_t>(FromScaledTextValue<int64_t>(reg, str) & 0xffffffff));
            break;
        case S64:
            value.Set(static_cast<uint64_t>(FromScaledTextValue<int64_t>(reg, str)));
            break;
        case U8:
            value.Set(FromScaledTextValue<uint64_t>(reg, str) & 0xff);
            break;
        case U16:
            value.Set(FromScaledTextValue<uint64_t>(reg, str) & 0xffff);
            break;
        case U24:
            value.Set(FromScaledTextValue<uint64_t>(reg, str) & 0xffffff);
            break;
        case U32:
            value.Set(FromScaledTextValue<uint64_t>(reg, str) & 0xffffffff);
            break;
        case U64:
            value.Set(FromScaledTextValue<uint64_t>(reg, str));
            break;
        case Float: {
            float v = FromScaledTextValue<double>(reg, str);
            uint64_t raw = 0;
            memcpy(&raw, &v, sizeof(v));
            value.Set(raw);
            break;
        }
        case Double: {
            double v = FromScaledTextValue<double>(reg, str);
            uint64_t raw = 0;
            memcpy(&raw, &v, sizeof(v));
            value.Set(raw);
            break;
        }
        case Char8:
            str.empty() ? value.Set(0) : value.Set(uint8_t(str[0]));
            break;
        case BCD8:
            value.Set(IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFF, WordSizes::W8_SZ));
            break;
        case BCD16:
            value.Set(IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFF, WordSizes::W16_SZ));
            break;
        case BCD24:
            value.Set(IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFFFF, WordSizes::W24_SZ));
            break;
        case BCD32:
            value.Set(IntToPackedBCD(FromScaledTextValue<uint64_t>(reg, str) & 0xFFFFFFFF, WordSizes::W32_SZ));
            break;
        case String:
        case String8:
            value.Set(str);
            break;
        default:
            value.Set(FromScaledTextValue<uint64_t>(reg, str));
    }
    return value;
}

TRegisterValue ConvertToRawValue(const TRegisterConfig& reg, const std::string& str)
{
    return GetRawValue(reg, str);
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

std::string ConvertFromRawValue(const TRegisterConfig& reg, TRegisterValue val)
{
    switch (reg.Format) {
        case U8:
            return ToScaledTextValue(reg, val.Get<uint8_t>());
        case S8:
            return ToScaledTextValue(reg, val.Get<int8_t>());
        case S16:
            return ToScaledTextValue(reg, val.Get<int16_t>());
        case S24: {
            uint32_t v = val.Get<uint64_t>() & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return ToScaledTextValue(reg, static_cast<int32_t>(v));
        }
        case S32:
            return ToScaledTextValue(reg, val.Get<int32_t>());
        case S64:
            return ToScaledTextValue(reg, val.Get<int64_t>());
        case BCD8:
            return ToScaledTextValue(reg, PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W8_SZ));
        case BCD16:
            return ToScaledTextValue(reg, PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W16_SZ));
        case BCD24:
            return ToScaledTextValue(reg, PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W24_SZ));
        case BCD32:
            return ToScaledTextValue(reg, PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W32_SZ));
        case Float: {
            float v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return ToScaledTextValue(reg, v);
        }
        case Double: {
            double v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return ToScaledTextValue(reg, v);
        }
        case Char8:
            return std::string(1, val.Get<uint8_t>());
        case String:
        case String8:
            return val.Get<std::string>();
        default:
            return ToScaledTextValue(reg, val.Get<uint64_t>());
    }
}

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val)
{
    switch (reg.Format) {
        case U8:
            return val.Get<uint8_t>();
        case S8:
            return val.Get<int8_t>();
        case S16:
            return val.Get<int16_t>();
        case S24: {
            uint32_t v = val.Get<uint64_t>() & 0xffffff;
            if (v & 0x800000)
                v |= 0xff000000;
            return static_cast<int32_t>(v);
        }
        case S32:
            return val.Get<int32_t>();
        case S64:
            return val.Get<int64_t>();
        case BCD8:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W8_SZ);
        case BCD16:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W16_SZ);
        case BCD24:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W24_SZ);
        case BCD32:
            return PackedBCD2Int(val.Get<uint64_t>(), WordSizes::W32_SZ);
        case Float: {
            float v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return v;
        }
        case Double: {
            double v;
            auto rawValue = val.Get<uint64_t>();
            memcpy(&v, &rawValue, sizeof(v));
            return v;
        }
        case Char8:
            return std::string(1, val.Get<uint8_t>());
        case String:
        case String8:
            return val.Get<std::string>();
        default:
            return val.Get<uint64_t>();
    }
}
