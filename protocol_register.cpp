#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;


bool TProtocolRegister::InitExternalLinkage(const PSerialDevice & device)
{
    struct TSerialDeviceLinkage: TProtocolRegister::IExternalLinkage
    {
        PWSerialDevice      Device;
        TProtocolRegister & Register;
        uint8_t             BitWidth;


        TSerialDeviceLinkage(const PSerialDevice & device, TProtocolRegister & self)
            : Device(device)
            , Register(self)
        {}

        PSerialDevice GetDevice() const override
        {
            auto device = Device.lock();
            assert(device);

            return device;
        }

        TPSet<PVirtualRegister> GetVirtualRegsiters() const override
        {
            return {};
        }

        void LinkWith(const PVirtualRegister &, const TProtocolRegisterBindInfo &) override
        {
            assert(false);
        }

        bool IsLinkedWith(const PVirtualRegister &) const override
        {
            return false;
        }

        uint8_t GetUsedByteCount() const override
        {
            return Register.Width;
        }

        const std::string & GetTypeName() const override
        {
            return GetDevice()->Protocol()->GetRegType(Register.Type).Name;
        }
    };

    if (ExternalLinkage && dynamic_cast<TSerialDeviceLinkage*>(ExternalLinkage.get())) {
        return false;
    }

    ExternalLinkage = utils::make_unique<TSerialDeviceLinkage>(device, *this);

    return true;
}

bool TProtocolRegister::InitExternalLinkage(const PVirtualRegister & reg, const TProtocolRegisterBindInfo & bindInfo)
{
    struct TVirtualRegisterLinkage: TProtocolRegister::IExternalLinkage
    {
        TPWSet<PWVirtualRegister> VirtualRegisters;
        uint8_t                   UsedBitCount;


        TVirtualRegisterLinkage(const PVirtualRegister & reg, const TProtocolRegisterBindInfo & bindInfo)
            : UsedBitCount(0)
        {
            LinkWithImpl(reg, bindInfo);
        }

        PVirtualRegister AssociatedVirtualRegister() const
        {
            assert(!VirtualRegisters.empty());

            auto virtualReg = VirtualRegisters.begin()->lock();
            assert(virtualReg);

            return virtualReg;
        }

        bool Has(const PVirtualRegister & reg) const
        {
            return find_if(VirtualRegisters.begin(), VirtualRegisters.end(), [&](const PWVirtualRegister & added){
                auto lockedAdded = added.lock();
                assert(lockedAdded);
                return lockedAdded == reg;
            }) != VirtualRegisters.end();
        }

        void LinkWithImpl(const PVirtualRegister & reg, const TProtocolRegisterBindInfo & bindInfo)
        {
            VirtualRegisters.insert(reg);

            /**
             * Find out how many bytes we have to read to cover all bits required by virtual registers
             *  thus, if, for instance, there is single virtual register that uses 1 last bit (64th),
             *  we'll have to read all 8 bytes in order to fill that single bit
             */
            UsedBitCount = max(UsedBitCount, bindInfo.BitEnd);
        }

        TPSet<PVirtualRegister> GetVirtualRegsiters() const override
        {
            assert(!VirtualRegisters.empty());

            TPSet<PVirtualRegister> result;

            for (const auto & virtualRegister: VirtualRegisters) {
                const auto & locked = virtualRegister.lock();
                assert(locked);

                bool inserted = result.insert(locked).second;
                assert(inserted);
            }

            return result;
        }

        PSerialDevice GetDevice() const
        {
            assert(!VirtualRegisters.empty());

            return AssociatedVirtualRegister()->GetDevice();
        }

        void LinkWith(const PVirtualRegister & reg, const TProtocolRegisterBindInfo & bindInfo) override
        {
            assert(!VirtualRegisters.empty());
            assert(GetDevice() == reg->GetDevice());
            assert(AssociatedVirtualRegister()->Type == reg->Type);
            assert(!Has(reg));

            auto itOtherReg = VirtualRegisters.lower_bound(reg);

            // check for overlapping after insertion point
            if (itOtherReg != VirtualRegisters.end()) {
                if (auto otherReg = itOtherReg->lock())
                    if (otherReg->AreOverlapping(*reg))
                        throw TSerialDeviceException("registers " + reg->ToStringWithFormat() + " and " + otherReg->ToStringWithFormat() + " are overlapping");
            }

            // check for overlapping before insertion point
            if (itOtherReg != VirtualRegisters.begin()) {
                if (auto otherReg = (--itOtherReg)->lock())
                    if (otherReg->AreOverlapping(*reg))
                        throw TSerialDeviceException("registers " + reg->ToStringWithFormat() + " and " + otherReg->ToStringWithFormat() + " are overlapping");
            }

            LinkWithImpl(reg, bindInfo);
        }

        bool IsLinkedWith(const PVirtualRegister & reg) const override
        {
            assert(!VirtualRegisters.empty());

            return VirtualRegisters.count(reg);
        }

        uint8_t GetUsedByteCount() const override
        {
            assert(!VirtualRegisters.empty());

            return BitCountToByteCount(UsedBitCount);
        }

        const std::string & GetTypeName() const override
        {
            assert(!VirtualRegisters.empty());

            return AssociatedVirtualRegister()->TypeName;
        }
    };

    if (ExternalLinkage && dynamic_cast<TVirtualRegisterLinkage*>(ExternalLinkage.get())) {
        return false;
    }

    ExternalLinkage = utils::make_unique<TVirtualRegisterLinkage>(reg, bindInfo);
    return true;
}

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type, uint8_t width)
    : Value(0)
    , Address(address)
    , Type(type)
    , Width(width)
{}

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type, const PSerialDevice & device)
    : TProtocolRegister(
        address,
        type,
        RegisterFormatByteWidth(device->Protocol()->GetRegType(type).DefaultFormat))
{
    InitExternalLinkage(device);
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

void TProtocolRegister::AssociateWith(const PVirtualRegister & reg, const TProtocolRegisterBindInfo & bindInfo)
{
    if (!InitExternalLinkage(reg, bindInfo)) {
        ExternalLinkage->LinkWith(reg, bindInfo);
    }
}

bool TProtocolRegister::IsAssociatedWith(const PVirtualRegister & reg) const
{
    assert(ExternalLinkage);

    return ExternalLinkage->IsLinkedWith(reg);
}

bool TProtocolRegister::IsReady() const
{
    return bool(ExternalLinkage);
}

const string & TProtocolRegister::GetTypeName() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetTypeName();
}

PSerialDevice TProtocolRegister::GetDevice() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetDevice();
}

TPSet<PVirtualRegister> TProtocolRegister::GetVirtualRegsiters() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetVirtualRegsiters();
}

uint8_t TProtocolRegister::GetUsedByteCount() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetUsedByteCount();
}

std::string TProtocolRegister::Describe() const
{
    return GetTypeName() + " register " + to_string(Address) + " of device " + GetDevice()->ToString();
}
