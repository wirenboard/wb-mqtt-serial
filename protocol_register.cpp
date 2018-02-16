#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type)
    : Address(address)
    , Type(type)
{}

bool TProtocolRegister::operator<(const TProtocolRegister & rhs)
{
    return Type < rhs.Type || (Type == rhs.Type && Address < rhs.Address);
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

    return intervals;
}

const string & TProtocolRegister::GetTypeName() const
{
    return AccessAssociatedVirtualRegister()->TypeName;
}

PSerialDevice TProtocolRegister::GetDevice() const
{
    return AccessAssociatedVirtualRegister()->GetDevice();
}

PVirtualRegister TProtocolRegister::AccessAssociatedVirtualRegister() const
{
    assert(!VirtualRegisters.empty());

    auto virtualReg = VirtualRegisters.rbegin()->lock();

    assert(virtualReg);

    return virtualReg;
}
