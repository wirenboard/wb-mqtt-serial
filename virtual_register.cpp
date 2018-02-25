#include "virtual_register.h"
#include "protocol_register.h"
#include "serial_device.h"
#include "types.h"
#include "bcd_utils.h"
#include "ir_device_query.h"
#include "binary_semaphore.h"

#include <wbmqtt/utils.h>

#include <cassert>
#include <cmath>

using namespace std;

namespace // utility
{
    union TAddress
    {
        uint64_t AbsoluteAddress;
        struct {
            uint32_t Type;
            uint32_t Address;
        };
    };

    template<typename T>
    inline T RoundValue(T val, double round_to)
    {
        static_assert(std::is_floating_point<T>::value, "TRegisterHandler::RoundValue accepts only floating point types");
        return round_to > 0 ? std::round(val / round_to) * round_to : val;
    }

    template<>
    inline std::string ToScaledTextValue(const TVirtualRegister & reg, float val)
    {
        return StringFormat("%.7g", RoundValue(reg.Scale * val + reg.Offset, reg.RoundTo));
    }

    template<>
    inline std::string ToScaledTextValue(const TVirtualRegister & reg, double val)
    {
        return StringFormat("%.15g", RoundValue(reg.Scale * val + reg.Offset, reg.RoundTo));
    }

    template<typename A>
    inline std::string ToScaledTextValue(const TVirtualRegister & reg, A val)
    {
        if (reg.Scale == 1 && reg.Offset == 0 && reg.RoundTo == 0) {
            return std::to_string(val);
        } else {
            return StringFormat("%.15g", RoundValue(reg.Scale * val + reg.Offset, reg.RoundTo));
        }
    }

    template<typename T>
    T FromScaledTextValue(const TVirtualRegister & reg, const std::string& str);

    template<>
    inline uint64_t FromScaledTextValue(const TVirtualRegister & reg, const std::string& str)
    {
        if (reg.Scale == 1 && reg.Offset == 0) {
            return std::stoull(str);
        } else {
            return round((RoundValue(stod(str), reg.RoundTo) - reg.Offset) / reg.Scale);
        }
    }

    template<>
    inline int64_t FromScaledTextValue(const TVirtualRegister & reg, const std::string& str)
    {
        if (reg.Scale == 1 && reg.Offset == 0) {
            return std::stoll(str);
        } else {
            return round((RoundValue(stod(str), reg.RoundTo) - reg.Offset) / reg.Scale);
        }
    }

    template<>
    inline double FromScaledTextValue(const TVirtualRegister & reg, const std::string& str)
    {
        return (RoundValue(stod(str), reg.RoundTo) - reg.Offset) / reg.Scale;
    }

    uint64_t InvertWordOrderIfNeeded(const TVirtualRegister & reg, const uint64_t value)
    {
        if (reg.WordOrder == EWordOrder::BigEndian) {
            return value;
        }

        uint64_t result = 0;
        uint64_t cur_value = value;

        for (int i = 0; i < reg.Width(); ++i) {
            uint16_t last_word = (((uint64_t) cur_value) & 0xFFFF);
            result <<= 16;
            result |= last_word;
            cur_value >>= 16;
        }
        return result;
    }

    string ConvertSlaveValue(const TVirtualRegister & reg, uint64_t value)
    {
        switch (reg.Format) {
        case S8:
            return ToScaledTextValue(reg, int8_t(value & 0xff));
        case S16:
            return ToScaledTextValue(reg, int16_t(value & 0xffff));
        case S24:
            {
                uint32_t v = value & 0xffffff;
                if (v & 0x800000) // fix sign (TBD: test)
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
        case Float:
            {
                union {
                    uint32_t raw;
                    float v;
                } tmp;
                tmp.raw = value;
                return ToScaledTextValue(reg, tmp.v);
            }
        case Double:
            {
                union {
                    uint64_t raw;
                    double v;
                } tmp;
                tmp.raw = value;
                return ToScaledTextValue(reg, tmp.v);
            }
        case Char8:
            return std::string(1, value & 0xff);
        default:
            return ToScaledTextValue(reg, value);
        }
    }

    uint64_t ConvertMasterValue(const TVirtualRegister & reg, const std::string& str)
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
        case Float:
            {
                union {
                    uint32_t raw;
                    float v;
                } tmp;

                tmp.v = FromScaledTextValue<double>(reg, str);
                return tmp.raw;
            }
        case Double:
            {
                union {
                    uint64_t raw;
                    double v;
                } tmp;
                tmp.v = FromScaledTextValue<double>(reg, str);
                return tmp.raw;
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

    inline uint64_t MersenneNumber(uint8_t bitCount)
    {
        assert(bitCount <= 64);
        return (uint64_t(1) << bitCount) - 1;
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context)
    : TRegisterConfig(*config)
    , Device(device)
    , FlushNeeded(flushNeeded)
    , Dirty(false)
    , ValueToWrite(0)
    , ValueWasAccepted(false)
    , ReadValue(0)
    , WriteQuery(nullptr)
    , ErrorState(EErrorState::UnknownErrorState)
{
    const auto & regType = Device->Protocol()->GetRegTypes()->at(TypeName);

    ProtocolRegisterWidth = RegisterFormatByteWidth(regType.DefaultFormat);

    TAddress protocolRegisterAddress;

    ProtocolRegisters = TProtocolRegister::GenerateProtocolRegisters(config, device, [&](int address) -> PProtocolRegister & {
        protocolRegisterAddress.Type    = config->Type;
        protocolRegisterAddress.Address = address;

        return context[make_pair(device, protocolRegisterAddress.AbsoluteAddress)];
    });

    if (!ReadOnly) {
        const auto & protocolInfo = device->GetProtocolInfo();

        if (protocolInfo.IsSingleBitType(Type)) {
            WriteQuery = std::make_shared<TIRDeviceSingleBitQuery>(GetProtocolRegisters());
        } else {
            WriteQuery = std::make_shared<TIRDevice64BitQuery>(GetProtocolRegisters());
        }
    }
}

uint32_t TVirtualRegister::GetBitPosition() const
{
    return (uint64_t(Address) * ProtocolRegisterWidth * 8) + BitOffset;
}

uint32_t TVirtualRegister::GetBitSize() const
{
    return RegisterFormatByteWidth(Format) * 8;
}

void TVirtualRegister::AssociateWithProtocolRegisters()
{
    for (const auto protocolRegisterBindInfo: ProtocolRegisters) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;

        assert(Type == protocolRegister->Type);
        protocolRegister->AssociateWith(shared_from_this());
    }
}

bool TVirtualRegister::UpdateReadError(bool error)
{
    EErrorState newState;
    if (error) {
        newState = (ErrorState == EErrorState::WriteError || ErrorState == EErrorState::ReadWriteError)
                    ? EErrorState::ReadWriteError
                    : EErrorState::ReadError;
    } else {
        newState = (ErrorState == EErrorState::ReadWriteError || ErrorState == EErrorState::WriteError)
                    ? EErrorState::WriteError
                    : EErrorState::NoError;
    }

    swap(ErrorState, newState);

    return ErrorState != newState;
}

bool TVirtualRegister::UpdateWriteError(bool error)
{
    EErrorState newState;
    if (error) {
        newState = (ErrorState == EErrorState::ReadError || ErrorState == EErrorState::ReadWriteError)
                    ? EErrorState::ReadWriteError
                    : EErrorState::WriteError;
    } else {
        newState = ErrorState == EErrorState::ReadWriteError ||
            ErrorState == EErrorState::ReadError ?
            EErrorState::ReadError : EErrorState::NoError;
    }

    swap(ErrorState, newState);

    return ErrorState != newState;
}

bool TVirtualRegister::AcceptDeviceValue(bool & changed)
{
    auto new_value = GetValue();

    changed = false;

    bool firstPoll = !ValueWasAccepted;
    ValueWasAccepted = true;

    if (HasErrorValue && ErrorValue == new_value) {
        if (true) {     // TODO: debug only
            std::cerr << "register " << ToString() << " contains error value" << std::endl;
        }
        return UpdateReadError(true);
    }

    if (ReadValue != new_value) {
        ReadValue = new_value;

        if (true) {     // TODO: debug only
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "new val for " << ToString() << ": " << std::hex << new_value << std::endl;
            std::cerr.flags(f);
        }
        changed = true;
        return UpdateReadError(false);
    }

    changed = firstPoll;
    return UpdateReadError(false);
}

bool TVirtualRegister::AcceptDeviceReadError()
{
    return UpdateReadError(true);
}

size_t TVirtualRegister::GetHash() const noexcept
{
    // NOTE: non-continious virtual registers are not supported yet by config, when they will, this must change as well
    using UniqueInterval = tuple<PSerialDevice, int, uint8_t, uint8_t>;

    return hash<UniqueInterval>()(UniqueInterval{Device, Address, BitOffset, BitWidth});
}

bool TVirtualRegister::operator==(const TVirtualRegister & rhs) const noexcept
{
    if (this == &rhs) {
        return true;
    }

    if (Device != rhs.Device) {
        return false;
    }

    auto lhsBegin = GetBitPosition();
    auto rhsBegin = rhs.GetBitPosition();
    if (lhsBegin != rhsBegin) {
        return false;
    }

    auto lhsEnd = lhsBegin + GetBitSize();
    auto rhsEnd = rhsBegin + rhs.GetBitSize();
    if (lhsEnd != rhsEnd) {
        return false;
    }

    return ProtocolRegisters == rhs.ProtocolRegisters;
}

const PSerialDevice & TVirtualRegister::GetDevice() const
{
    return Device;
}

const TPSet<TProtocolRegister> & TVirtualRegister::GetProtocolRegisters() const
{
    return GetKeysAsSet(ProtocolRegisters);
}

bool TVirtualRegister::NeedToPublish() const
{
    // NOTE: it is highly likely that registers will be read in order, so we check in reverse order to detect negative result faster
    return all_of(ProtocolRegisters.rbegin(), ProtocolRegisters.rend(), [](const pair<PProtocolRegister, TRegisterBindInfo> & item) {
        return item.second.IsRead;
    });
}

EErrorState TVirtualRegister::GetErrorState() const
{
    return ErrorState;
}

uint64_t TVirtualRegister::GetValue() const
{
    return MapValueFrom(ProtocolRegisters);
}

std::string TVirtualRegister::GetTextValue() const
{
    return ConvertSlaveValue(*this, InvertWordOrderIfNeeded(*this, GetValue()));
}

void TVirtualRegister::SetTextValue(const std::string & value)
{
    if (ReadOnly) {
        cerr << "WARNING: attempt to write to read-only register. Ignored" << endl;
        return;
    }
    Dirty.store(true);
    ValueToWrite = InvertWordOrderIfNeeded(*this, ConvertMasterValue(*this, value));
    FlushNeeded->Signal();
}

bool TVirtualRegister::NeedToFlush() const
{
    return Dirty.load();
}

bool TVirtualRegister::Flush()
{
    if (Dirty.load()) {
        Dirty.store(false);

        MapValueTo(WriteQuery, ProtocolRegisters, ValueToWrite);

        Device->Execute(WriteQuery);

        return UpdateWriteError(WriteQuery->GetStatus() != EQueryStatus::OK);
    }

    return false;
}

void TVirtualRegister::NotifyPublished()
{
    for (auto & registerBindInfo: ProtocolRegisters) {
        registerBindInfo.second.IsRead = false;
    }
}

bool TVirtualRegister::NotifyRead(const PProtocolRegister & reg, bool ok)
{
    if (!ProtocolRegisters.count(reg)) {
        return false;
    }

    ProtocolRegisters.at(reg).IsRead = ok;

    return true;
}


PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context)
{
    auto reg = make_shared<TVirtualRegister>(config, device, flushNeeded, context);
    reg->AssociateWithProtocolRegisters();

    return reg;
}

void TVirtualRegister::MapValueFromIteration(const PProtocolRegister & reg, const TRegisterBindInfo & bindInfo, uint64_t & value, uint8_t & bitPosition)
{
    assert(bindInfo.IsRead);

    auto mask = MersenneNumber(bindInfo.BitCount());
    value |= ((mask & reg->Value >> bindInfo.BitStart) << bitPosition);

    bitPosition += bindInfo.BitCount();
}

uint64_t TVirtualRegister::MapValueFrom(const TPSet<TProtocolRegister> & registers, const vector<TRegisterBindInfo> & bindInfos)
{
    uint64_t value;

    uint8_t bitPosition = 0;
    auto bindInfo = bindInfos.begin();
    for (const auto & protocolRegister: registers) {
        MapValueFromIteration(protocolRegister, *bindInfo++, value, bitPosition);
    }

    return value;
}

uint64_t TVirtualRegister::MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> & registersBindInfo)
{
    uint64_t value;

    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterBindInfo: registersBindInfo) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        MapValueFromIteration(protocolRegister, bindInfo, value, bitPosition);
    }

    return value;
}

void TVirtualRegister::MapValueTo(const PIRDeviceValueQuery & query, const TPMap<TProtocolRegister, TRegisterBindInfo> & registerMap, uint64_t value)
{
    size_t index = 0;
    for (const auto & protocolRegisterBindInfo: registerMap) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        auto valueLocalMask = MersenneNumber(bindInfo.BitCount());
        auto registerLocalMask = valueLocalMask << bindInfo.BitStart;

        auto cachedRegisterValue = protocolRegister->Value;

        auto registerValue = (~registerLocalMask & cachedRegisterValue) | (valueLocalMask & value) << bindInfo.BitStart;

        query->SetValue(index++, registerValue);

        value >>= bindInfo.BitCount();
    }
}
