#include "register.h"

TRegisterRange::TRegisterRange(const std::list<PRegister>& regs): RegList(regs)
{
    if (RegList.empty())
        throw std::runtime_error("cannot construct empty register range");
    PRegister first = regs.front();
    RegSlave = first->Slave;
    RegType = first->Type;
    RegTypeName = first->TypeName;
    RegPollInterval = first->PollInterval;
}

TRegisterRange::TRegisterRange(PRegister reg): RegList(1, reg)
{
    RegSlave = reg->Slave;
    RegType = reg->Type;
    RegTypeName = reg->TypeName;
    RegPollInterval = reg->PollInterval;
}

TRegisterRange::~TRegisterRange() {}

TSimpleRegisterRange::TSimpleRegisterRange(const std::list<PRegister>& regs): TRegisterRange(regs) {}

TSimpleRegisterRange::TSimpleRegisterRange(PRegister reg): TRegisterRange(reg) {}

void TSimpleRegisterRange::Reset()
{
    Values.clear();
    Errors.clear();
}

void TSimpleRegisterRange::SetValue(PRegister reg, uint64_t value)
{
    Values[reg] = value;
}

void TSimpleRegisterRange::SetError(PRegister reg)
{
    Errors.insert(reg);
}

void TSimpleRegisterRange::MapRange(TValueCallback value_callback, TErrorCallback error_callback)
{
    for (auto reg: RegisterList()) {
        if (Errors.find(reg) == Errors.end())
            value_callback(reg, Values[reg]);
        else
            error_callback(reg);
    }
}
