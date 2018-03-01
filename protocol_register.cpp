#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type)
    : Value(0)
    , Enabled(true)
    , Address(address)
    , Type(type)
{}

TPMap<PProtocolRegister, TRegisterBindInfo> TProtocolRegister::GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device, TRegisterCache && cache)
{
    TPMap<PProtocolRegister, TRegisterBindInfo> registersBindInfo;

    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    const uint8_t registerBitWidth = RegisterFormatByteWidth(regType.DefaultFormat) * 8;
    auto bitsToAllocate = config->GetBitWidth();

    cerr << "bits: " << (int)bitsToAllocate << endl;

    uint32_t regCount = BitCountToRegCount(bitsToAllocate, registerBitWidth);

    cerr << "registers: " << regCount << endl;

    cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;

    for (auto regIndex = 0u, regIndexEnd = regCount; regIndex != regIndexEnd; ++regIndex) {
        auto type    = config->Type;
        auto address = config->Address + regIndex;

        TRegisterBindInfo bindInfo {};

        bindInfo.BitStart = 0;
        bindInfo.BitEnd = min(registerBitWidth, bitsToAllocate);

        auto localBitShift = max(int(config->BitOffset) - int(regIndex * registerBitWidth), 0);

        bindInfo.BitStart = min(uint16_t(localBitShift), uint16_t(bindInfo.BitEnd));

        if (bindInfo.BitStart == bindInfo.BitEnd) {
            if (bitsToAllocate) {
                ++regIndexEnd;
            }
            continue;
        }

        PProtocolRegister _protocolRegisterFallback = nullptr;

        auto & protocolRegister = (cache ? cache(address) : _protocolRegisterFallback);

        if (!protocolRegister) {
            cerr << "create protocol register at " << address << endl;
            protocolRegister = make_shared<TProtocolRegister>(address, type);
        }

        bitsToAllocate -= bindInfo.BitCount();

        registersBindInfo[protocolRegister] = bindInfo;
    }

    return move(registersBindInfo);
}

bool TProtocolRegister::operator<(const TProtocolRegister & rhs) const
{
    return Type < rhs.Type || (Type == rhs.Type && Address < rhs.Address);
}

bool TProtocolRegister::operator==(const TProtocolRegister & rhs) const
{
    if (this == &rhs) {
        return true;
    }

    return Type == rhs.Type && Address == rhs.Address && GetDevice() == rhs.GetDevice();
}

void TProtocolRegister::AssociateWith(const PVirtualRegister & reg)
{
    if (!VirtualRegisters.empty()) {
        assert(GetDevice() == reg->GetDevice());
    }

    assert((int)Type == reg->Type);

    bool inserted = VirtualRegisters.insert(reg).second;

    if (!inserted) {
        throw TSerialDeviceException("register instersection at " + reg->ToString());
    }
}

bool TProtocolRegister::IsAssociatedWith(const PVirtualRegister & reg)
{
    return VirtualRegisters.count(reg);
}

std::set<TIntervalMs> TProtocolRegister::GetPollIntervals() const
{
    std::set<TIntervalMs> intervals;
    for (const auto & wVirtualRegister: VirtualRegisters) {
        auto virtualRegister = wVirtualRegister.lock();

        assert(virtualRegister);

        intervals.insert(virtualRegister->PollInterval);
    }

    // remove multiples of each other intervals and keep the smallest of them in set
    // NOTE: maybe replace with explicit check inside cycle (more expensive, but more obvious and maybe more reliable)
    for (auto itInterval1 = intervals.begin(); itInterval1 != intervals.end(); ++itInterval1) {
        for (auto itInterval2 = itInterval1; itInterval2 != intervals.end(); ++itInterval2) {
            if (itInterval1 == itInterval2) {
                continue;
            }

            auto i = *itInterval1, j = *itInterval2;

            if (i.count() % j.count() == 0) {
                if (i < j) {
                    itInterval2 = intervals.erase(itInterval2);
                } else {
                    itInterval1 = intervals.erase(itInterval1);
                }
            }
        }
    }

    return intervals;
}

const string & TProtocolRegister::GetTypeName() const
{
    return AssociatedVirtualRegister()->TypeName;
}

PSerialDevice TProtocolRegister::GetDevice() const
{
    if (VirtualRegisters.empty()) {
        return nullptr;
    }

    return AssociatedVirtualRegister()->GetDevice();
}

TPUnorderedSet<PVirtualRegister> TProtocolRegister::GetVirtualRegsiters() const
{
    TPUnorderedSet<PVirtualRegister> result;

    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & locked = virtualRegister.lock();

        assert(locked);

        bool inserted = result.insert(locked).second;

        assert(inserted);
    }

    return result;
}

uint8_t TProtocolRegister::GetUsedByteCount() const
{
    uint8_t bitCount = 0;

    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & locked = virtualRegister.lock();

        assert(locked);

        bitCount = max(bitCount, locked->GetUsedBitCount(const_pointer_cast<TProtocolRegister>(shared_from_this())));
    }

    return BitCountToByteCount(bitCount);
}

PVirtualRegister TProtocolRegister::AssociatedVirtualRegister() const
{
    assert(!VirtualRegisters.empty());

    auto virtualReg = VirtualRegisters.begin()->lock();

    assert(virtualReg);

    return virtualReg;
}

void TProtocolRegister::DisableIfNotUsed()
{
    bool used = false;

    for (const auto & reg: VirtualRegisters) {
        auto virtualRegister = reg.lock();

        if (!virtualRegister) {
            continue;
        }

        used |= virtualRegister->IsEnabled();

        if (used) {
            break;
        }
    }

    if (!used) {
        Enabled = false;    // no need to notify associated virtual registers - they are all already disabled
    }
}

void TProtocolRegister::SetReadValue(uint64_t value)
{
    Value = value;

    SetReadError(false);
}

void TProtocolRegister::SetWriteValue(uint64_t value)
{
    Value = value;

    SetWriteError(false);
}

void TProtocolRegister::SetReadError(bool error)
{
    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & reg = virtualRegister.lock();

        assert(reg);

        bool found = reg->NotifyRead(shared_from_this(), !error);

        assert(found);
    }
}

void TProtocolRegister::SetWriteError(bool error)
{
    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & reg = virtualRegister.lock();

        assert(reg);

        bool found = reg->NotifyWrite(shared_from_this(), !error);

        assert(found);
    }
}

bool TProtocolRegister::IsEnabled() const
{
    return Enabled;
}
