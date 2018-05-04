#include "memory_block.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>
#include <string.h>

using namespace std;


bool TMemoryBlock::InitExternalLinkage(const PSerialDevice & device)
{
    struct TSerialDeviceLinkage: TMemoryBlock::IExternalLinkage
    {
        PWSerialDevice      Device;
        TMemoryBlock & Register;


        TSerialDeviceLinkage(const PSerialDevice & device, TMemoryBlock & self)
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

bool TMemoryBlock::InitExternalLinkage(const PVirtualRegister & reg)
{
    struct TVirtualRegisterLinkage: TMemoryBlock::IExternalLinkage
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

TMemoryBlock::TMemoryBlock(uint32_t address, uint16_t size, const TMemoryBlockType & type)
    : Cache(nullptr)
    , Address(address)
    , Type(type)
    , Size(type.IsVariadicSize() ? size : type.Size)
{
    assert(Size < 8192 && "memory block size must be less than 8192 bytes");
}

TMemoryBlock::TMemoryBlock(uint32_t address, const TMemoryBlockType & type)
    : TMemoryBlock(address, type.Size, type)
{
    assert(!type.IsVariadicSize() && "memory block with variadic size must be created with size specified");
}

TMemoryBlock::TMemoryBlock(uint32_t address, uint16_t size, uint32_t typeIndex, const PSerialDevice & device)
    : TMemoryBlock(
        address,
        size,
        device->Protocol()->GetRegType(typeIndex)
    )
{
    InitExternalLinkage(device);
}

TMemoryBlock::TMemoryBlock(uint32_t address, uint32_t typeIndex, const PSerialDevice & device)
    : TMemoryBlock(
        address,
        device->Protocol()->GetRegType(typeIndex)
    )
{
    InitExternalLinkage(device);
}

bool TMemoryBlock::operator<(const TMemoryBlock & rhs) const
{
    return Type.Index < rhs.Type.Index || (Type.Index == rhs.Type.Index && Address < rhs.Address);
}

bool TMemoryBlock::operator==(const TMemoryBlock & rhs) const
{
    if (this == &rhs) {
        return true;
    }

    return Type.Index == rhs.Type.Index && Address == rhs.Address && GetDevice() == rhs.GetDevice();
}

void TMemoryBlock::AssociateWith(const PVirtualRegister & reg)
{
    if (!InitExternalLinkage(reg)) {
        ExternalLinkage->LinkWith(reg);
    }
}

bool TMemoryBlock::IsAssociatedWith(const PVirtualRegister & reg) const
{
    assert(ExternalLinkage);

    return ExternalLinkage->IsLinkedWith(reg);
}

bool TMemoryBlock::IsReady() const
{
    return bool(ExternalLinkage);
}

const string & TMemoryBlock::GetTypeName() const
{
    return Type.Name;
}

PSerialDevice TMemoryBlock::GetDevice() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetDevice();
}

TPSet<PVirtualRegister> TMemoryBlock::GetVirtualRegsiters() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->GetVirtualRegsiters();
}

bool TMemoryBlock::NeedsCaching() const
{
    assert(ExternalLinkage);

    return ExternalLinkage->NeedsCaching();
}

void TMemoryBlock::AssignCache(uint8_t * cache)
{
    assert(NeedsCaching());
    assert(!Cache);

    Cache = cache;

    if (Global::Debug) {
        cerr << "Adding to cache " << Describe() << endl;
    }
}

void TMemoryBlock::CacheIfNeeded(const uint8_t * data)
{
    assert(NeedsCaching() == bool(Cache));

    if (Cache) {
        memcpy(Cache, data, Size);
    }
}

uint8_t TMemoryBlock::GetCachedByte(uint16_t index) const
{
    assert(index < Size);

    if (Cache) {
        return Cache[Type.ByteOrder == EByteOrder::BigEndian ? index : (Size - index - 1)];
    }

    return 0x00;
}

std::string TMemoryBlock::Describe() const
{
    return GetTypeName() + " memory block " + to_string(Address) + " of device " + GetDevice()->ToString();
}
