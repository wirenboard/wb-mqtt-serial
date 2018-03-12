#include "protocol_register.h"
#include "serial_device.h"
#include "virtual_register.h"

#include <cassert>

using namespace std;


void TProtocolRegister::InitExternalLinkage(const PSerialDevice & device)
{
    struct TSerialDeviceLinkage: TProtocolRegister::IExternalLinkage
    {
        PWSerialDevice          Device;
        TProtocolRegister &     Register;


        TSerialDeviceLinkage(const PSerialDevice & device, TProtocolRegister & reg)
            : Device(device)
            , Register(reg)
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

        void LinkWith(const PProtocolRegister &, const PVirtualRegister &) override
        {
            assert(false);
        }

        bool IsLinkedWith(const PVirtualRegister &) const override
        {
            return false;
        }

        uint8_t GetUsedByteCount() const override
        {
            return RegisterFormatByteWidth(GetDevice()->Protocol()->GetRegType(Register.Type).DefaultFormat);
        }

        const std::string & GetTypeName() const override
        {
            return GetDevice()->Protocol()->GetRegType(Register.Type).Name;
        }
    };

    if (ExternalLinkage && dynamic_cast<TSerialDeviceLinkage*>(ExternalLinkage.get())) {
        return;
    }

    ExternalLinkage = utils::make_unique<TSerialDeviceLinkage>(device, *this);
}

void TProtocolRegister::InitExternalLinkage(const PVirtualRegister & reg)
{
    struct TVirtualRegisterLinkage: TProtocolRegister::IExternalLinkage
    {
        TPWSet<PWVirtualRegister> VirtualRegisters;
        uint8_t                   UsedBitCount;


        TVirtualRegisterLinkage(const PVirtualRegister & reg)
            : VirtualRegisters({ reg })
            , UsedBitCount(0)
        {}

        PVirtualRegister AssociatedVirtualRegister() const
        {
            assert(!VirtualRegisters.empty());

            auto virtualReg = VirtualRegisters.begin()->lock();

            assert(virtualReg);

            return virtualReg;
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

        void LinkWith(const PProtocolRegister & self, const PVirtualRegister & reg) override
        {
            assert(!VirtualRegisters.empty());
            assert(GetDevice() == reg->GetDevice());
            assert(AssociatedVirtualRegister()->Type == reg->Type);

            bool inserted = VirtualRegisters.insert(reg).second;

            if (!inserted) {
                throw TSerialDeviceException("register collision at " + reg->ToString());
            }

            /**
             * Find out how many bytes we have to read to cover all bits required by virtual registers
             *  thus, if, for instance, there is single virtual register that uses 1 last bit (64th),
             *  we'll have to read all 8 bytes in order to fill that single bit
             */

            const auto & bindInfo = reg->GetBindInfo(self);

            UsedBitCount = max(UsedBitCount, bindInfo.BitEnd);
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
        return;
    }

    ExternalLinkage = utils::make_unique<TVirtualRegisterLinkage>(reg);
}

TProtocolRegister::TProtocolRegister(uint32_t address, uint32_t type, const PSerialDevice & device)
    : Address(address)
    , Type(type)
    , Value(0)
{
    if (device) {
        InitExternalLinkage(device);
    }
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
    InitExternalLinkage(reg);

    ExternalLinkage->LinkWith(shared_from_this(), reg);
}

bool TProtocolRegister::IsAssociatedWith(const PVirtualRegister & reg) const
{
    assert(ExternalLinkage);

    return ExternalLinkage->IsLinkedWith(reg);
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
