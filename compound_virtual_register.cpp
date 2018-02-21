#include "compound_virtual_register.h"


TCompoundVirtualRegister::TCompoundVirtualRegister(std::vector<PVirtualRegister> && virtualRegisters)
    : VirtualRegisters(move(virtualRegisters))
{}


size_t TCompoundVirtualRegister::GetHash() const noexcept
{

}

bool TCompoundVirtualRegister::operator==(const TVirtualRegister & rhs) const noexcept
{

}

const PSerialDevice & TCompoundVirtualRegister::GetDevice() const
{

}

const TPSet<TProtocolRegister> & TCompoundVirtualRegister::GetProtocolRegisters() const
{

}

bool TCompoundVirtualRegister::IsReady() const
{

}

void TCompoundVirtualRegister::ResetReady()
{

}

EErrorState TCompoundVirtualRegister::GetErrorState() const
{

}

std::string TCompoundVirtualRegister::GetTextValue() const
{

}

void TCompoundVirtualRegister::SetTextValue(const std::string & value)
{

}

bool TCompoundVirtualRegister::NeedToFlush() const
{

}

bool TCompoundVirtualRegister::Flush()
{

}

bool TCompoundVirtualRegister::NotifyRead(const PProtocolRegister &)
{

}
