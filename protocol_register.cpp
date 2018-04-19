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

        void LinkWith(const PVirtualRegister &) override
        {
            assert(false);
        }

        bool IsLinkedWith(const PVirtualRegister &) const override
        {
            return false;
        }

        bool NeedsCaching() const override
        {
            return false;
        }
    };

    if (ExternalLinkage && dynamic_cast<TSerialDeviceLinkage*>(ExternalLinkage.get())) {
        return false;
    }

    ExternalLinkage = utils::make_unique<TSerialDeviceLinkage>(device, *this);

    return true;
}

bool TProtocolRegister::InitExternalLinkage(const PVirtualRegister & reg)
{
    struct TVirtualRegisterLinkage: TProtocolRegister::IExternalLinkage
    {
        TPWSet<PWVirtualRegister> VirtualRegisters;


        TVirtualRegisterLinkage(const PVirtualRegister & reg)
        {
            LinkWithImpl(reg);
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

        void LinkWithImpl(const PVirtualRegister & reg)
        {
            VirtualRegisters.insert(reg);
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

        void LinkWith(const PVirtualRegister & reg) override
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

            LinkWithImpl(reg);
        }

        bool IsLinkedWith(const PVirtualRegister & reg) const override
        {
            assert(!VirtualRegisters.empty());

            return VirtualRegisters.count(reg);
        }

        /* If single memory block is associated with more than 1 different
         *  virtual registers, it means that none of those virtual registers
         *  covers block entierly (because overlapping is forbidden),
         *  thus if any of associated virtual registers
         *  is writable then we need to cache its value,
         *  otherwise it will corrupt non-covered part of memory block at write
         */
        bool NeedsCaching() const override
        {
            assert(!VirtualRegisters.empty());

            return (
                VirtualRegisters.size() > 1 &&
                any_of(VirtualRegisters.begin(), VirtualRegisters.end(), [](const PWVirtualRegister & reg) {
                    return !reg.lock()->ReadOnly;
                })
            );
        }
    };

    if (ExternalLinkage && dynamic_cast<TVirtualRegisterLinkage*>(ExternalLinkage.get())) {
        return false;
    }

    ExternalLinkage = utils::make_unique<TVirtualRegisterLinkage>(reg);
    return true;
}

TProtocolRegister::TProtocolRegister(uint32_t address, uint16_t size, const TMemoryBlockType & type)
    : Cache(nullptr)
    , Address(address)
    , Type(type)
    , Size(size)
{
    assert(Size < 8192 && "memory block size must be less than 8192 bytes");
}

TProtocolRegister::TProtocolRegister(uint32_t address, const TMemoryBlockType & type)
    : TProtocolRegister(address, type.Size, type)
{
    assert(!type.IsVariadicSize() && "memory block with variadic size must be created with size specified");
}

TProtocolRegister::TProtocolRegister(uint32_t address, uint16_t size, uint32_t typeIndex, const PSerialDevice & device)
    : TProtocolRegister(
        address,
        size,
        device->Protocol()->GetRegType(typeIndex)
    )
{
    InitExternalLinkage(device);
}

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t typeIndex, const PSerialDevice & device)
    : TProtocolRegister(
        address,
        device->Protocol()->GetRegType(typeIndex)
    )
{
    InitExternalLinkage(device);
}

bool TProtocolRegister::operator<(const TProtocolRegister & rhs) const
{
    return Type.Index < rhs.Type.Index || (Type.Index == rhs.Type.Index && Address < rhs.Address);
}

bool TProtocolRegister::operator==(const TProtocolRegister & rhs) const
{
    if (this == &rhs) {
        return true;
    }

    return Type.Index == rhs.Type.Index && Address == rhs.Address && GetDevice() == rhs.GetDevice();
}

void TProtocolRegister::AssociateWith(const PVirtualRegister & reg)
{
    if (!InitExternalLinkage(reg)) {
        ExternalLinkage->LinkWith(reg);
    }

    if (ExternalLinkage->NeedsCaching() && !Cache) {
        Cache = GetDevice()->AllocateCacheMemory(Size);
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
    return Type.Name;
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

std::string TProtocolRegister::Describe() const
{
    return GetTypeName() + " register " + to_string(Address) + " of device " + GetDevice()->ToString();
}
