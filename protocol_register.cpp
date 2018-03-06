#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type)
    : Address(address)
    , Type(type)
    , Value(0)
    , UsedBitCount(0)
{}

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
        throw TSerialDeviceException("register collision at " + reg->ToString());
    }

    /**
     * Find out how many bytes we have to read to cover all bits required by virtual registers
     *  thus, if, for instance, there is single virtual register that uses 1 last bit (64th),
     *  we'll have to read all 8 bytes in order to fill that single bit
     */

    const auto & bindInfo = reg->GetBindInfo(const_pointer_cast<TProtocolRegister>(shared_from_this()));

    UsedBitCount = max(UsedBitCount, bindInfo.BitEnd);
}

bool TProtocolRegister::IsAssociatedWith(const PVirtualRegister & reg)
{
    return VirtualRegisters.count(reg);
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
    return BitCountToByteCount(UsedBitCount);
}

PVirtualRegister TProtocolRegister::AssociatedVirtualRegister() const
{
    assert(!VirtualRegisters.empty());

    auto virtualReg = VirtualRegisters.begin()->lock();

    assert(virtualReg);

    return virtualReg;
}

void TProtocolRegister::SetValue(uint64_t value)
{
    Value = value;
}

std::string TProtocolRegister::Describe() const
{
    return GetTypeName() + " register " + to_string(Address) + " of device " + GetDevice()->ToString();
}
