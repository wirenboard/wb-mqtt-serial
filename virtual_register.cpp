#include "virtual_register.h"
#include "serial_device.h"
#include "ir_device_query.h"
#include "bcd_utils.h"
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

PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context)
{
    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    PVirtualRegister virtualRegister (new TVirtualRegister(config, device, flushNeeded));

    virtualRegister->ProtocolRegisterWidth = RegisterFormatByteWidth(regType.DefaultFormat);

    TAddress protocolRegisterAddress;

    virtualRegister->ProtocolRegisters = TProtocolRegister::GenerateProtocolRegisters(config, device, [&](int address) -> PProtocolRegister & {
        protocolRegisterAddress.Type    = config->Type;
        protocolRegisterAddress.Address = address;

        return context[make_pair(device, protocolRegisterAddress.AbsoluteAddress)];
    });
}

static uint64_t MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> & registerMap)
{
    uint64_t value;

    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterReady: registerMap) {
        const auto & protocolRegister = protocolRegisterReady.first;
        const auto & bindInfo = protocolRegisterReady.second;

        assert(bindInfo.IsRead);

        auto absBegin = bindInfo.BitStart + bitPosition;

        auto mask = MersenneNumber(bindInfo.BitCount());
        value |= ((mask & protocolRegister->LastValue) << absBegin);

        bitPosition += bindInfo.BitCount();
    }

    return value;
}

static void MapValueTo(const TPMap<TProtocolRegister, TRegisterBindInfo> & registerMap, uint64_t value)
{
    uint8_t bitPosition = 0;
    for (const auto & protocolRegisterReady: registerMap) {
        const auto & protocolRegister = protocolRegisterReady.first;
        const auto & bindInfo = protocolRegisterReady.second;

        auto mask = MersenneNumber(bindInfo.BitCount());

        auto cachedValue = protocolRegister->LastValue;

        auto absBegin = bindInfo.BitStart + bitPosition;

        auto registerValue = (~mask & cachedValue) | (mask & (value >> absBegin));

        protocolRegister->SetValueFromClient(registerValue);

        bitPosition += bindInfo.BitCount();
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded)
    : TRegisterConfig(*config)
    , Device(device)
    , FlushNeeded(flushNeeded)
    , Dirty(false)
    , ValueToWrite(0)
    , ValueWasAccepted(false)
    , ReadValue(0)
    , WriteQuerySet(nullptr)
    , ErrorState(EErrorState::UnknownErrorState)
{}

uint32_t TVirtualRegister::GetBitPosition() const
{
    return (uint64_t(Address) * ProtocolRegisterWidth * 8) + BitOffset;
}

uint32_t TVirtualRegister::GetBitSize() const
{
    return ProtocolRegisterWidth * 8 * ProtocolRegisters.size();
}

void TVirtualRegister::AssociateWith(const PProtocolRegister & reg, const TRegisterBindInfo & bindInfo)
{
    if (!ProtocolRegisters.empty() && ProtocolRegisters.begin()->first->Type != reg->Type) {
        throw TSerialDeviceException("different protocol register types within same virtual register are not supported");
    }

    bool inserted = ProtocolRegisters.insert({reg, bindInfo}).second;

    assert(inserted);

    reg->AssociateWith(shared_from_this());
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

bool TVirtualRegister::AcceptDeviceValue(bool * changed)
{
    auto new_value = GetValue();

    bool _changePlaceholder;
    if (changed == nullptr) {
        changed = &_changePlaceholder;
    }

    *changed = false;

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
        *changed = true;
        return UpdateReadError(false);
    }

    *changed = firstPoll;
    return UpdateReadError(false);
}

bool TVirtualRegister::AcceptDeviceReadError()
{
    return UpdateReadError(true);
}

bool TVirtualRegister::operator<(const TVirtualRegister & rhs) const noexcept
{
    auto lhsEnd = GetBitPosition() + GetBitSize();
    auto rhsBegin = rhs.GetBitPosition();

    return lhsEnd <= rhsBegin;
}

const PSerialDevice & TVirtualRegister::GetDevice() const
{
    return Device;
}

const TPSet<TProtocolRegister> & TVirtualRegister::GetProtocolRegisters() const
{
    return ProtocolRegisters;
}

bool TVirtualRegister::IsReady() const
{
    // it is highly likely that registers will be read in order, so we check in reverse order to detect negative result faster
    return all_of(ProtocolRegisters.rbegin(), ProtocolRegisters.rend(), [](const pair<PProtocolRegister, TRegisterBindInfo> & item) {
        return item.second.IsRead;
    });
}

void TVirtualRegister::ResetReady()
{
    for (auto & registerBindInfo: ProtocolRegisters) {
        registerBindInfo.second.IsRead = false;
    }
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
    Dirty = true;
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

        bool error = false;

        for (const auto & query: WriteQuerySet->Queries) {
            Device->Write(*query);

            error |= query->Status != EQueryStatus::OK;
        }

        return UpdateWriteError(error);
    }

    return false;
}

void TVirtualRegister::NotifyRead(const PProtocolRegister & reg)
{
    assert(ProtocolRegisters.count(reg));

    ProtocolRegisters.at(reg).IsRead = true;
}
