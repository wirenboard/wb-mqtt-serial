#include "register.h"
#include "serial_device.h"
#include "protocol_register.h"
#include "virtual_register.h"

TRegisterRange::TRegisterRange(const std::list<PProtocolRegister>& regs): RegList(regs)
{
    if (RegList.empty())
        throw std::runtime_error("cannot construct empty register range");
    PProtocolRegister first = regs.front();
    RegDevice = first->GetParent()->GetDevice();
    RegType = first->Type;
    RegTypeName = first->TypeName;
}

TRegisterRange::TRegisterRange(PProtocolRegister reg): RegList(1, reg)
{
    RegDevice = reg->GetParent()->GetDevice();
    RegType = reg->Type;
    RegTypeName = reg->TypeName;
}

TRegisterRange::~TRegisterRange() {}

TSimpleRegisterRange::TSimpleRegisterRange(const std::list<PProtocolRegister>& regs): TRegisterRange(regs) {}

TSimpleRegisterRange::TSimpleRegisterRange(PProtocolRegister reg): TRegisterRange(reg) {}

void TSimpleRegisterRange::Reset()
{
    Values.clear();
    Errors.clear();
}

void TSimpleRegisterRange::SetValue(PProtocolRegister reg, uint64_t value)
{
    Values[reg] = value;
}

void TSimpleRegisterRange::SetError(PProtocolRegister reg)
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

TRegisterRange::EStatus TSimpleRegisterRange::GetStatus() const
{
    return Errors.size() == RegisterList().size() ? ST_UNKNOWN_ERROR: ST_OK;
}

bool TSimpleRegisterRange::NeedsSplit() const
{
    return false;
}

std::string TRegister::ToString() const
{
    return "<" + Device()->ToString() + ":" + TRegisterConfig::ToString() + ">";
}

std::map<std::tuple<PSerialDevice, PRegisterConfig>, PRegister> TRegister::RegStorage;
std::mutex TRegister::Mutex;
