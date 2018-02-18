#include "virtual_register.h"
#include "serial_device.h"

#include <cassert>
#include <cmath>

using namespace std;

namespace // utility
{
    union TAddress
    {
        int64_t AbsoluteAddress;
        struct {
            uint32_t Type;
            uint32_t Address;
        };
    };
}

PVirtualRegister TVirtualRegister::Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext & context)
{
    const auto & regType = device->Protocol()->GetRegTypes()->at(config->TypeName);

    PVirtualRegister virtualRegister (new TVirtualRegister(config));

    virtualRegister->ProtocolRegisterWidth = RegisterFormatByteWidth(regType.DefaultFormat);
    virtualRegister->Device = device;

    auto regCount = max((uint32_t)ceil(float(config->GetBitWidth()) / (virtualRegister->ProtocolRegisterWidth * 8)), 1u);

    cerr << "split " << config->ToString() << " to " << regCount << " " << config->TypeName << " registers" << endl;

    for (uint32_t regIndex = 0; regIndex < regCount; ++regIndex) {
        TAddress protocolRegisterAddress;

        protocolRegisterAddress.Type    = config->Type;
        protocolRegisterAddress.Address = config->Address + regIndex;

        auto & protocolRegister = context[make_pair(device, protocolRegisterAddress.AbsoluteAddress)];

        if (!protocolRegister) {
            protocolRegister = make_shared<TProtocolRegister>(protocolRegisterAddress.Address, protocolRegisterAddress.Type);
        }

        virtualRegister->AssociateWith(protocolRegister);
    }
}

TVirtualRegister::TVirtualRegister(const PRegisterConfig & config)
    : TRegisterConfig(*config)
{}

uint32_t TVirtualRegister::GetBitPosition() const
{
    return (uint64_t(Address) * ProtocolRegisterWidth * 8) + BitOffset;
}

uint32_t TVirtualRegister::GetBitSize() const
{
    return ProtocolRegisterWidth * 8 * ProtocolRegisters.size();
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

void TVirtualRegister::AssociateWith(const PProtocolRegister & reg)
{
    if (!ProtocolRegisters.empty() && (*ProtocolRegisters.begin())->Type != reg->Type) {
        throw TSerialDeviceException("different protocol register types within same virtual register are not supported");
    }

    bool inserted = ProtocolRegisters.insert({reg, false}).second;

    assert(inserted);

    reg->AssociateWith(shared_from_this());
}

void TVirtualRegister::NotifyRead(const PProtocolRegister & reg)
{
    assert(ProtocolRegisters.count(reg));

    ProtocolRegisters.at(reg) = true;
}
