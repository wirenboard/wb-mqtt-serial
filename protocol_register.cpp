#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type)
    : Address(address)
    , Type(type)
    , Value(0)
    , Enabled(true)
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
        throw TSerialDeviceException("register instersection at " + reg->ToString());
    }
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

std::string TProtocolRegister::Describe() const
{
    return GetTypeName() + " register " + to_string(Address) + " of device " + GetDevice()->ToString();
}
