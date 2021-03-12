#include "register.h"
#include "serial_device.h"

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

void TRegisterRange::SetError()
{
    for (auto& r: RegList) {
        r->SetError();
    }
}

TSimpleRegisterRange::TSimpleRegisterRange(const std::list<PRegister>& regs): TRegisterRange(regs) {}

TSimpleRegisterRange::TSimpleRegisterRange(PRegister reg): TRegisterRange(reg) {}

TRegisterRange::EStatus TSimpleRegisterRange::GetStatus() const
{
    if (std::all_of(RegisterList().begin(), RegisterList().end(), [](const PRegister& r) {return r->GetError();})) {
        return ST_UNKNOWN_ERROR;
    }
    return ST_OK;
}

std::string TRegisterConfig::ToString() const {
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
    return TRegisterConfig::ToString() + " of device " + Device()->ToString();
}

bool TRegister::IsAvailable() const
{
    return Available;
}

void TRegister::SetAvailable(bool available)
{
    Available = available;
}

bool TRegister::GetError() const
{
    return Error;
}

void TRegister::SetError()
{
    Error = true;
}

uint64_t TRegister::GetValue() const
{
    return Value;
}

void TRegister::SetValue(uint64_t value)
{
    Value = value;
    Error = false;
    Available = true;
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

    if (BitOffset >= 16) {
        throw TSerialDeviceException("bit offset must not exceed 16 bits");
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

PRegisterConfig TRegisterConfig::Create(int                               type,
                                        std::shared_ptr<IRegisterAddress> address,
                                        RegisterFormat                    format,
                                        double                            scale,
                                        double                            offset,
                                        double                            round_to,
                                        bool                              poll,
                                        bool                              readonly,
                                        const std::string&                type_name,
                                        std::unique_ptr<uint64_t>         error_value,
                                        const EWordOrder                  word_order,
                                        uint8_t                           bit_offset,
                                        uint8_t                           bit_width,
                                        std::unique_ptr<uint64_t>         unsupported_value)
{
    return std::make_shared<TRegisterConfig>(type, address, format, scale, offset, round_to, poll, readonly,
                                             type_name, std::move(error_value), word_order, bit_offset,
                                             bit_width, std::move(unsupported_value));
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
    return Create(type, std::make_shared<TUint32RegisterAddress>(address), format, scale, offset, round_to, poll, readonly,
                  type_name, std::move(error_value), word_order, bit_offset,
                  bit_width, std::move(unsupported_value));
}

TUint32RegisterAddress::TUint32RegisterAddress(uint32_t address) : Address(address)
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
