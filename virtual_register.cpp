#include "virtual_register.h"
#include "protocol_register.h"
#include "protocol_register_factory.h"
#include "serial_device.h"
#include "types.h"
#include "bcd_utils.h"
#include "ir_device_query.h"
#include "binary_semaphore.h"
#include "virtual_register_set.h"

#include <wbmqtt/utils.h>

#include <cassert>
#include <cmath>
#include <tuple>
#include <bitset>

using namespace std;

namespace // utility
{
    template<typename T>
    inline T RoundValue(T val, double round_to)
    {
        static_assert(std::is_floating_point<T>::value, "TRegisterHandler::RoundValue accepts only floating point types");
        return round_to > 0 ? std::round(val / round_to) * round_to : val;
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

        for (int i = 0; i < reg.GetFormatWordWidth(); ++i) {
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

    template <typename T>
    inline size_t Hash(const T & value)
    {
        return hash<T>()(value);
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device)
    : TRegisterConfig(*config)
    , Device(device)
    , FlushNeeded(nullptr)
    , ValueToWrite(0)
    , CurrentValue(0)
    , WriteQuery(nullptr)
    , ErrorState(EErrorState::UnknownErrorState)
    , ChangedPublishData(EPublishData::None)
    , Dirty(false)
    , Enabled(true)
    , ValueIsRead(false)
    , ValueWasAccepted(false)
{}

void TVirtualRegister::Initialize()
{
    auto device = Device.lock();
    auto self = shared_from_this();

    assert(device);

    ProtocolRegisters = TProtocolRegisterFactory::GenerateProtocolRegisters(self, device);

    for (const auto protocolRegisterBindInfo: ProtocolRegisters) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        assert(Type == (int)protocolRegister->Type);
        protocolRegister->AssociateWith(self, bindInfo);
    }

    if (!ReadOnly) {
        TIRDeviceQuerySet querySet({GetProtocolRegisters()}, EQueryOperation::Write);
        assert(querySet.Queries.size() == 1);

        WriteQuery = dynamic_pointer_cast<TIRDeviceValueQuery>(*querySet.Queries.begin());
        assert(WriteQuery);
    }

    if (Global::Debug)
        cerr << "New virtual register: " << Describe() << endl;
}

uint32_t TVirtualRegister::GetBitPosition() const
{
    return GetBitEnd() - GetBitWidth();
}

uint32_t TVirtualRegister::GetBitEnd() const
{
    assert(GetFormatBitWidth() > BitOffset);

    return (uint32_t(Address) * ProtocolRegisters.begin()->first->Width * 8) + GetFormatBitWidth() - BitOffset;
}

const PIRDeviceValueQuery & TVirtualRegister::GetWriteQuery() const
{
    return WriteQuery;
}

void TVirtualRegister::WriteValueToQuery()
{
    MapValueTo(WriteQuery, ProtocolRegisters, ValueToWrite);
}

uint64_t TVirtualRegister::ComposeValue() const
{
    assert(ValueIsRead);

    return MapValueFrom(ProtocolRegisters);
}

void TVirtualRegister::AcceptDeviceValue(bool ok)
{
    if (!NeedToPoll()) {
        return;
    }

    if (!ok) {
        return UpdateReadError(true);
    }

    auto new_value = ComposeValue();

    bool firstPoll = !ValueWasAccepted;
    ValueWasAccepted = true;

    if (HasErrorValue && ErrorValue == new_value) {
        if (Global::Debug) {
            std::cerr << "register " << ToString() << " contains error value" << std::endl;
        }
        return UpdateReadError(true);
    }

    if (CurrentValue != new_value) {
        CurrentValue = new_value;

        if (Global::Debug) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "new val for " << ToString() << ": " << std::hex << new_value << std::endl;
            std::cerr.flags(f);
        }
        Add(ChangedPublishData, EPublishData::Value);
        return UpdateReadError(false);
    }

    if (firstPoll) {
        Add(ChangedPublishData, EPublishData::Value);
    }
    return UpdateReadError(false);
}

size_t TVirtualRegister::GetHash() const noexcept
{
    // NOTE: non-continious virtual registers are not supported yet by config, when they will, this must change as well
    return Hash(GetDevice()) ^ Hash(Address) ^ Hash(BitOffset) ^ Hash(BitWidth);
}

bool TVirtualRegister::operator==(const TVirtualRegister & rhs) const noexcept
{
    if (this == &rhs) {
        return true;
    }

    if (GetDevice() != rhs.GetDevice()) {
        return false;
    }

    auto lhsBegin = GetBitPosition();
    auto rhsBegin = rhs.GetBitPosition();
    if (lhsBegin != rhsBegin) {
        return false;
    }

    auto lhsEnd = lhsBegin + GetBitWidth();
    auto rhsEnd = rhsBegin + rhs.GetBitWidth();
    if (lhsEnd != rhsEnd) {
        return false;
    }

    // NOTE: need to check registers and bind info for non-continious virtual registers
    return true; // ProtocolRegisters == rhs.ProtocolRegisters;
}

bool TVirtualRegister::operator<(const TVirtualRegister & rhs) const noexcept
{
    assert(GetDevice() == rhs.GetDevice());     // comparison makes sense only if registers are of same device

    return Type < rhs.Type || (Type == rhs.Type && GetBitPosition() < rhs.GetBitPosition());
}

void TVirtualRegister::SetFlushSignal(PBinarySemaphore flushNeeded)
{
    FlushNeeded = flushNeeded;
}

PSerialDevice TVirtualRegister::GetDevice() const
{
    return Device.lock();
}

TPSet<PProtocolRegister> TVirtualRegister::GetProtocolRegisters() const
{
    return GetKeysAsSet(ProtocolRegisters);
}

EErrorState TVirtualRegister::GetErrorState() const
{
    return ErrorState;
}

uint64_t TVirtualRegister::GetValue() const
{
    return CurrentValue;
}

std::string TVirtualRegister::Describe() const
{
    ostringstream ss;

    ss << "[" << endl;

    for (const auto & protocolRegisterBindInfo: ProtocolRegisters) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        ss << "\t" << protocolRegister->Address << ": " << bindInfo.Describe() << endl;
    }

    ss << "]";

    return ss.str();
}

std::string TVirtualRegister::GetTextValue() const
{
    auto textValue = ConvertSlaveValue(*this, InvertWordOrderIfNeeded(*this, GetValue()));

    return OnValue.empty() ? textValue : (textValue == OnValue ? "1" : "0");
}

void TVirtualRegister::SetTextValue(const std::string & value)
{
    if (ReadOnly) {
        cerr << "WARNING: attempt to write to read-only register. Ignored" << endl;
        return;
    }

    SetValue(InvertWordOrderIfNeeded(*this, ConvertMasterValue(*this, OnValue.empty() ? value : (value == "1" ? OnValue : "0"))));
}

void TVirtualRegister::SetValue(uint64_t value)
{
    Dirty.store(true);
    ValueToWrite = value;

    if (FlushNeeded) {
        FlushNeeded->Signal();
    }
}

bool TVirtualRegister::NeedToPoll() const
{
    return Poll && !Dirty;
}

bool TVirtualRegister::IsChanged(EPublishData state) const
{
    return Has(ChangedPublishData, state);
}

bool TVirtualRegister::NeedToFlush() const
{
    return Dirty.load();
}

void TVirtualRegister::Flush()
{
    if (Dirty.load()) {
        Dirty.store(false);

        assert(WriteQuery);

        MapValueTo(WriteQuery, ProtocolRegisters, ValueToWrite);

        WriteQuery->ResetStatus();

        GetDevice()->Execute(WriteQuery);

        UpdateWriteError(WriteQuery->GetStatus() != EQueryStatus::Ok);
    }
}

bool TVirtualRegister::IsEnabled() const
{
    return Enabled;
}

void TVirtualRegister::SetEnabled(bool enabled)
{
    Enabled = enabled;

    if (Global::Debug) {
        cerr << (Enabled ? "re-enabled" : "disabled") << " register: " << ToString() << endl;
    }
}

std::string TVirtualRegister::ToString() const
{
    return "<" + GetDevice()->ToString() + ":" + TRegisterConfig::ToString() + ">";
}

bool TVirtualRegister::AreOverlapping(const TVirtualRegister & other) const
{
    if (GetDevice() == other.GetDevice() && Type == other.Type) {
        const auto start1 = GetBitPosition(),
                   start2 = other.GetBitPosition(),
                   end1   = GetBitEnd(),
                   end2   = other.GetBitEnd();

        return end1 > start2 && end2 > start1;
    }

    return false;
}

void TVirtualRegister::AssociateWithSet(const PVirtualRegisterSet & virtualRegisterSet)
{
    VirtualRegisterSet = virtualRegisterSet;
}

PAbstractVirtualRegister TVirtualRegister::GetTopLevel()
{
    if (const auto & virtualRegisterSet = VirtualRegisterSet.lock()) {
        return virtualRegisterSet;
    } else {
        return shared_from_this();
    }
}

void TVirtualRegister::ResetChanged(EPublishData state)
{
    Remove(ChangedPublishData, state);
}

bool TVirtualRegister::GetValueIsRead() const
{
    return ValueIsRead;
}

void TVirtualRegister::UpdateReadError(bool error)
{
    EErrorState before = ErrorState;

    if (ErrorState == EErrorState::UnknownErrorState) {
        ErrorState = EErrorState::NoError;
    }

    if (error) {
        Add(ErrorState, EErrorState::ReadError);
    } else {
        Remove(ErrorState, EErrorState::ReadError);
    }

    if (ErrorState != before) {
        Add(ChangedPublishData, EPublishData::Error);
        if (Global::Debug) {
            cerr << "UpdateReadError: changed error to " << (int)ErrorState << endl;
        }
    }
}

void TVirtualRegister::UpdateWriteError(bool error)
{
    EErrorState before = ErrorState;

    if (ErrorState == EErrorState::UnknownErrorState) {
        ErrorState = EErrorState::NoError;
    }

    if (error) {
        Add(ErrorState, EErrorState::WriteError);
    } else {
        Remove(ErrorState, EErrorState::WriteError);
    }

    if (ErrorState != before) {
        Add(ChangedPublishData, EPublishData::Error);
        if (Global::Debug) {
            cerr << "UpdateWriteError: changed error to " << (int)ErrorState << endl;
        }
    }
}

void TVirtualRegister::NotifyRead(bool ok)
{
    if (Global::Debug) {
        cerr << "NotifyRead for: " << ToString() << ": " << (ok ? "OK" : "ERROR") << endl;
    }

    if (!NeedToPoll()) {
        return;
    }

    assert(!ValueIsRead);

    ValueIsRead = ok;

    AcceptDeviceValue(ok);
}

void TVirtualRegister::NotifyWrite(bool ok)
{
    UpdateWriteError(!ok);

    if (ok) {
        CurrentValue = ValueToWrite;
    }
}

void TVirtualRegister::InvalidateReadValues()
{
    ValueIsRead = false;
}

PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device)
{
    auto reg = PVirtualRegister(new TVirtualRegister(config, device));
    reg->Initialize();

    return reg;
}

uint64_t TVirtualRegister::MapValueFrom(const TPMap<PProtocolRegister, TProtocolRegisterBindInfo> & registerMap)
{
    uint64_t value = 0;

    uint8_t bitPosition = 0;
    for (auto protocolRegisterBindInfo = registerMap.rbegin(); protocolRegisterBindInfo != registerMap.rend(); ++protocolRegisterBindInfo) {
        const auto & protocolRegister = protocolRegisterBindInfo->first;
        const auto & bindInfo = protocolRegisterBindInfo->second;

        auto mask = MersenneNumber(bindInfo.BitCount()) << bitPosition;
        value |= mask & ((protocolRegister->Value >> bindInfo.BitStart) << bitPosition);

        if (Global::Debug) {
            cerr << "reg mask: " << bitset<64>(mask) << endl;
            cerr << "reading " << bindInfo.Describe() << " bits of " << protocolRegister->Describe()
                 << " to [" << (int)bitPosition << ", " << int(bitPosition + bindInfo.BitCount() - 1) << "] bits of value" << endl;
        }

        bitPosition += bindInfo.BitCount();
    }

    if (Global::Debug)
        cerr << "map value from registers: " << value << endl;

    return value;
}

void TVirtualRegister::MapValueTo(const PIRDeviceValueQuery & query, const TPMap<PProtocolRegister, TProtocolRegisterBindInfo> & registerMap, uint64_t value)
{
    if (Global::Debug)
        cerr << "map value to registers: " << value << endl;

    uint8_t bitPosition = 0;
    for (auto protocolRegisterBindInfo = registerMap.rbegin(); protocolRegisterBindInfo != registerMap.rend(); ++protocolRegisterBindInfo) {
        const auto & protocolRegister = protocolRegisterBindInfo->first;
        const auto & bindInfo = protocolRegisterBindInfo->second;

        auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

        const auto & cachedRegisterValue = protocolRegister->GetValue();

        auto registerValue = (~mask & cachedRegisterValue) | (mask & ((value >> bitPosition) << bindInfo.BitStart));

        if (Global::Debug) {
            cerr << "cached reg val: " << cachedRegisterValue << " (0x" << hex << cachedRegisterValue << dec << ")" << endl;
            cerr << "reg mask: " << bitset<64>(mask) << endl;
            cerr << "reg value: " << registerValue << " (0x" << hex << registerValue << dec << ")" << endl;
            cerr << "writing [" << (int)bitPosition << ", " << int(bitPosition + bindInfo.BitCount() - 1) << "]" << " bits of value "
                 << " to " << bindInfo.Describe() << " bits of " << protocolRegister->Describe() << endl;
        }

        query->SetValue(protocolRegister, registerValue);

        bitPosition += bindInfo.BitCount();
    }
}
