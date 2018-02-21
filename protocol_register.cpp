#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type)
    : Address(address)
    , Type(type)
    , Value(0)
{}

TPMap<TProtocolRegister, TRegisterBindInfo> TProtocolRegister::GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device, TRegisterCache && cache)
{
    TPMap<TProtocolRegister, TRegisterBindInfo> registersBindInfo;

    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    const uint8_t registerBitWidth = RegisterFormatByteWidth(regType.DefaultFormat) * 8;
    auto bitsToAllocate = config->GetBitWidth();

    auto regCount = max((uint32_t)ceil(float(bitsToAllocate) / registerBitWidth), 1u);

    cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;

    for (auto regIndex = 0u, regIndexEnd = regCount - 1; regIndex != regIndexEnd; ++regIndex) {
        auto type    = config->Type;
        auto address = config->Address + regIndex;

        TRegisterBindInfo bindInfo {};

        bindInfo.BitStart = regIndex * registerBitWidth;
        bindInfo.BitEnd = bindInfo.BitStart + min(registerBitWidth, bitsToAllocate);

        auto localBitShift = max(config->BitOffset - bindInfo.BitStart, 0);

        bindInfo.BitStart = min(uint16_t(bindInfo.BitStart + localBitShift), uint16_t(bindInfo.BitEnd));

        if (bindInfo.BitStart == bindInfo.BitEnd) {
            if (bitsToAllocate) {
                ++regIndexEnd;
            }
            continue;
        }

        PProtocolRegister _protocolRegisterFallback = nullptr;

        auto & protocolRegister = (cache ? cache(address) : _protocolRegisterFallback);

        if (!protocolRegister) {
            protocolRegister = make_shared<TProtocolRegister>(address, type);
        }

        bitsToAllocate -= bindInfo.BitCount();

        registersBindInfo[protocolRegister] = bindInfo;
    }

    return move(registersBindInfo);
}

bool TProtocolRegister::operator<(const TProtocolRegister & rhs)
{
    return Type < rhs.Type || (Type == rhs.Type && Address < rhs.Address);
}

bool TProtocolRegister::operator==(const TProtocolRegister & rhs)
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

    assert(Type == reg->Type);

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
    return AssociatedVirtualRegister()->GetTypeName();
}

PSerialDevice TProtocolRegister::GetDevice() const
{
    if (VirtualRegisters.empty()) {
        return nullptr;
    }

    return AssociatedVirtualRegister()->GetDevice();
}

TPSet<TVirtualRegister> TProtocolRegister::GetVirtualRegsiters() const
{
    TPSet<TVirtualRegister> result;

    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & locked = virtualRegister.lock();

        assert(locked);

        assert(result.insert(locked).second);
    }

    return result;
}

PVirtualRegister TProtocolRegister::AssociatedVirtualRegister() const
{
    assert(!VirtualRegisters.empty());

    auto virtualReg = VirtualRegisters.rbegin()->lock();

    assert(virtualReg);

    return virtualReg;
}

void TProtocolRegister::NotifyErrorFromDevice()
{

}

void TProtocolRegister::SetValueFromDevice(uint64_t value)
{
    Value = value;

    for (const auto & virtualRegister: VirtualRegisters) {
        const auto & reg = virtualRegister.lock();

        assert(reg);

        reg->NotifyRead(shared_from_this(), true);
    }
}

void TProtocolRegister::SetValueFromClient(uint64_t value)
{
    Value = value;
}
