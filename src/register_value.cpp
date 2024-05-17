#include "register_value.h"

namespace
{
    std::string GetTypeName(TRegisterValue::ValueType type)
    {
        switch (type) {
            case TRegisterValue::ValueType::Undefined:
                return "Undefined";
            case TRegisterValue::ValueType::Integer:
                return "Integer";
            case TRegisterValue::ValueType::String:
                return "String";
        }
        return "Unknown";
    }
}

TRegisterValueException::TRegisterValueException(const char* file, int line, const std::string& message)
    : WBMQTT::TBaseException(file, line, message)
{}

template<> uint64_t TRegisterValue::Get<>() const
{
    CheckIntegerValue();
    return IntegerValue;
}

template<> int64_t TRegisterValue::Get<>() const
{
    CheckIntegerValue();
    return static_cast<int64_t>(IntegerValue);
}

template<> uint16_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<uint16_t>(IntegerValue);
}

template<> int16_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<int16_t>(Get<uint16_t>());
}

template<> uint32_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<uint32_t>(IntegerValue);
}

template<> int32_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<int32_t>(Get<uint32_t>());
}

template<> uint8_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<uint8_t>(IntegerValue);
}

template<> int8_t TRegisterValue::Get() const
{
    CheckIntegerValue();
    return static_cast<int8_t>(Get<uint8_t>());
}

template<> std::string TRegisterValue::Get() const
{
    CheckStringValue();
    return StringValue;
}

TRegisterValue::TRegisterValue(uint64_t value)
{
    Set(value);
}

TRegisterValue::TRegisterValue(const std::string& stringValue)
{
    Set(stringValue);
}

void TRegisterValue::Set(uint64_t value)
{
    Type = ValueType::Integer;
    IntegerValue = value;
}

void TRegisterValue::Set(const std::string& value)
{
    Type = ValueType::String;
    StringValue = value;
}

TRegisterValue& TRegisterValue::operator=(const TRegisterValue& other)
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Type = other.Type;
    switch (other.Type) {
        case ValueType::String:
            StringValue = other.StringValue;
            break;
        case ValueType::Integer:
            IntegerValue = other.IntegerValue;
            break;
        default:
            break;
    }
    return *this;
}

TRegisterValue& TRegisterValue::operator=(TRegisterValue&& other) noexcept
{
    // Guard self assignment
    if (this == &other)
        return *this;

    Type = other.Type;
    switch (other.Type) {
        case ValueType::String:
            StringValue.swap(other.StringValue);
            break;
        case ValueType::Integer:
            IntegerValue = other.IntegerValue;
            break;
        default:
            break;
    }
    return *this;
}

bool TRegisterValue::operator==(const TRegisterValue& other) const
{
    if (Type != other.Type) {
        return false;
    }
    switch (Type) {
        case ValueType::String:
            return (StringValue == other.StringValue);
        case ValueType::Integer:
            return IntegerValue == other.IntegerValue;
        default:
            return true;
    }
}

bool TRegisterValue::operator==(uint64_t other) const
{
    return *this == TRegisterValue{other};
}

bool TRegisterValue::operator!=(const TRegisterValue& other) const
{
    return !(*this == other);
}

TRegisterValue::ValueType TRegisterValue::GetType() const
{
    return Type;
}

void TRegisterValue::CheckIntegerValue() const
{
    if (Type != ValueType::Integer) {
        wb_throw(TRegisterValueException, ("Value is not Integer: " + GetTypeName(Type)));
    }
}

void TRegisterValue::CheckStringValue() const
{
    if (Type != ValueType::String) {
        wb_throw(TRegisterValueException, ("Value is not String: " + GetTypeName(Type)));
    }
}

std::ostream& operator<<(std::ostream& os, const TRegisterValue& obj)
{
    switch (obj.GetType()) {
        case TRegisterValue::ValueType::Undefined: {
            os << "Undefined";
            break;
        }
        case TRegisterValue::ValueType::Integer: {
            os << obj.Get<uint64_t>();
            break;
        }
        case TRegisterValue::ValueType::String: {
            os << obj.Get<std::string>();
            break;
        }
    }
    return os;
}
