#include "virtual_register.h"
#include "protocol_register.h"
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

    template <typename T>
    inline size_t Hash(const T & value)
    {
        return hash<T>()(value);
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, TInitContext & context)
    : TRegisterConfig(*config)
    , Device(device)
    , FlushNeeded(nullptr)
    , ValueToWrite(0)
    , CurrentValue(0)
    , WriteQuery(nullptr)
    , ErrorState(EErrorState::UnknownErrorState)
    , ChangedPublishData(EPublishData::None)
    , ReadRegistersCount(0)
    , ValueWasAccepted(false)
    , IsRead(false)
    , Enabled(true)
    , Dirty(false)
{
    const auto & regType = device->Protocol()->GetRegTypes()->at(TypeName);

    ProtocolRegisterWidth = RegisterFormatByteWidth(regType.DefaultFormat);

    TAddress protocolRegisterAddress;

    ProtocolRegisters = TProtocolRegister::GenerateProtocolRegisters(config, device, [&](int address) -> PProtocolRegister & {
        protocolRegisterAddress.Type    = config->Type;
        protocolRegisterAddress.Address = address;

        return context[make_pair(device, protocolRegisterAddress.AbsoluteAddress)];
    });

    if (!ReadOnly) {
        TIRDeviceQuerySet querySet({GetProtocolRegisters()}, EQueryOperation::Write);
        assert(querySet.Queries.size() == 1);

        WriteQuery = dynamic_pointer_cast<TIRDeviceValueQuery>(*querySet.Queries.begin());
        assert(WriteQuery);
    }

    cerr << "New virtual register: " << Describe() << endl;
}

uint32_t TVirtualRegister::GetBitPosition() const
{
    return (uint32_t(Address) * ProtocolRegisterWidth * 8) + BitOffset;
}

uint8_t TVirtualRegister::GetBitSize() const
{
    return RegisterFormatByteWidth(Format) * 8;
}

uint8_t TVirtualRegister::GetUsedBitCount(const PProtocolRegister & reg) const
{
    try {
        return ProtocolRegisters.at(reg).BitCount();
    } catch (out_of_range &) {
        return 0;
    }
}

uint64_t TVirtualRegister::ComposeValue() const
{
    return MapValueFrom(ProtocolRegisters);
}

bool TVirtualRegister::AcceptDeviceValue(bool & changed, bool ok)
{
    changed = false;

    if (!NeedToPoll()) {
        return false;
    }

    if (!ok) {
        return UpdateReadError(true);
    }

    auto new_value = ComposeValue();

    bool firstPoll = !ValueWasAccepted;
    ValueWasAccepted = true;

    if (HasErrorValue && ErrorValue == new_value) {
        if (true) {     // TODO: debug only
            std::cerr << "register " << ToString() << " contains error value" << std::endl;
        }
        return UpdateReadError(true);
    }

    if (CurrentValue != new_value) {
        CurrentValue = new_value;

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

    auto lhsEnd = lhsBegin + GetBitSize();
    auto rhsEnd = rhsBegin + rhs.GetBitSize();
    if (lhsEnd != rhsEnd) {
        return false;
    }

    // NOTE: need to check registers and bind info for non-continious virtual registers
    return true; // ProtocolRegisters == rhs.ProtocolRegisters;
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

        ss << "\t" << protocolRegister->Address << ": [" << (int)bindInfo.BitStart << ", " << (int)bindInfo.BitEnd - 1 << "]" << endl;
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
    Dirty.store(true);
    ValueToWrite = InvertWordOrderIfNeeded(*this, ConvertMasterValue(*this, OnValue.empty() ? value : (value == "1" ? OnValue : "0")));
    FlushNeeded->Signal();
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

bool TVirtualRegister::Flush()
{
    if (Dirty.load()) {
        Dirty.store(false);

        assert(WriteQuery);

        MapValueTo(WriteQuery, ProtocolRegisters, ValueToWrite);

        GetDevice()->Execute(WriteQuery);

        bool ok = WriteQuery->GetStatus() != EQueryStatus::Ok;

        if (ok) {
            CurrentValue = ValueToWrite;
        }

        return UpdateWriteError(ok);
    }

    return false;
}

bool TVirtualRegister::IsEnabled() const
{
    return Enabled;
}

void TVirtualRegister::SetEnabled(bool enabled)
{
    swap(Enabled, enabled);

    if (Enabled != enabled) {
        if (Enabled) {
            cerr << "re-enabled register: " << Describe() << endl;

            for (const auto & regIndex: ProtocolRegisters) {
                regIndex.first->Enabled = true;
            }
        } else {
            cerr << "disabled register: " << Describe() << endl;

            for (const auto & regIndex: ProtocolRegisters) {
                regIndex.first->DisableIfNotUsed();
            }
        }
    }
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

void TVirtualRegister::NotifyPublished(EPublishData state)
{
    Remove(ChangedPublishData, state);
}

void TVirtualRegister::AssociateWithProtocolRegisters()
{
    for (const auto protocolRegisterBindInfo: ProtocolRegisters) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;

        assert(Type == (int)protocolRegister->Type);
        protocolRegister->AssociateWith(shared_from_this());
    }
}

bool TVirtualRegister::ValueIsRead() const
{
    if (IsRead) {
        return true;
    }

    // NOTE: it is highly likely that registers will be read in order, so we check in reverse order to detect negative result faster
    return IsRead = (ReadRegistersCount == ProtocolRegisters.size() &&
           all_of(ProtocolRegisters.rbegin(), ProtocolRegisters.rend(), [](const pair<PProtocolRegister, TRegisterBindInfo> & item) {
               return item.second.IsRead;
           }));
}

void TVirtualRegister::InvalidateProtocolRegisterValues()
{
    for (auto & registerBindInfo: ProtocolRegisters) {
        registerBindInfo.second.IsRead = TRegisterBindInfo::NotRead;
    }
    ReadRegistersCount = 0;
    IsRead = false;
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
        newState = (ErrorState == EErrorState::ReadWriteError || ErrorState == EErrorState::ReadError)
                    ? EErrorState::ReadError
                    : EErrorState::NoError;
    }

    swap(ErrorState, newState);

    return ErrorState != newState;
}

bool TVirtualRegister::NotifyRead(const PProtocolRegister & reg, bool ok)
{
    const auto & itProtocolRegisterBindInfo = ProtocolRegisters.find(reg);

    if (itProtocolRegisterBindInfo == ProtocolRegisters.end()) {
        return false;
    }

    if (!NeedToPoll()) {
        return true;
    }

    auto & bindInfo = itProtocolRegisterBindInfo->second;

    if (bindInfo.IsRead == TRegisterBindInfo::NotRead) {
        bindInfo.IsRead = ok ? TRegisterBindInfo::ReadOk : TRegisterBindInfo::ReadError;
        if (ok) {
            ++ReadRegistersCount;
            if (ValueIsRead()) {
                bool changed;
                if (AcceptDeviceValue(changed, ok)) {
                    Add(ChangedPublishData, EPublishData::Error);
                }

                if (changed) {
                    Add(ChangedPublishData, EPublishData::Value);
                }

                return true;
            }
        }
    }

    if (!ok) {
        if (UpdateReadError(true)) {
            Add(ChangedPublishData, EPublishData::Error);
        }
    }

    return true;
}

bool TVirtualRegister::NotifyWrite(const PProtocolRegister & reg, bool ok)
{
    if (!ProtocolRegisters.count(reg)) {
        return false;
    }

    if (UpdateWriteError(!ok)) {
        Add(ChangedPublishData, EPublishData::Error);
    }

    return true;
}

PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext & context)
{
    auto reg = PVirtualRegister(new TVirtualRegister(config, device, context));
    reg->AssociateWithProtocolRegisters();

    return reg;
}

void TVirtualRegister::MapValueFromIteration(const PProtocolRegister & reg, const TRegisterBindInfo & bindInfo, uint64_t & value, uint8_t & bitPosition)
{
    assert(bindInfo.IsRead);

    auto mask = MersenneNumber(bindInfo.BitCount());
    value |= ((mask & (reg->Value >> bindInfo.BitStart)) << bitPosition);

    bitPosition += bindInfo.BitCount();
}

uint64_t TVirtualRegister::MapValueFrom(const TPSet<PProtocolRegister> & registers, const vector<TRegisterBindInfo> & bindInfos)
{
    uint64_t value = 0;

    uint8_t bitPosition = 0;
    auto bindInfo = bindInfos.begin();
    for (const auto & protocolRegister: registers) {
        MapValueFromIteration(protocolRegister, *bindInfo++, value, bitPosition);
    }

    cerr << "map value from: " << value << endl;

    return value;
}

uint64_t TVirtualRegister::MapValueFrom(const TPMap<PProtocolRegister, TRegisterBindInfo> & registersBindInfo)
{
    uint64_t value = 0;

    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterBindInfo: registersBindInfo) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        MapValueFromIteration(protocolRegister, bindInfo, value, bitPosition);
    }

    cerr << "map value from: " << value << endl;

    return value;
}

void TVirtualRegister::MapValueTo(const PIRDeviceValueQuery & query, const TPMap<PProtocolRegister, TRegisterBindInfo> & registerMap, uint64_t value)
{
    for (const auto & protocolRegisterBindInfo: registerMap) {
        const auto & protocolRegister = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        auto valueLocalMask = MersenneNumber(bindInfo.BitCount());
        auto registerLocalMask = valueLocalMask << bindInfo.BitStart;

        auto cachedRegisterValue = protocolRegister->Value;

        auto registerValue = (~registerLocalMask & cachedRegisterValue) | (valueLocalMask & value) << bindInfo.BitStart;

        query->SetValue(protocolRegister, registerValue);

        value >>= bindInfo.BitCount();
    }
}
