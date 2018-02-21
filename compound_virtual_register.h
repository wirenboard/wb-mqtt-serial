#pragma once

#include "virtual_register.h"

#include <vector>


class TCompoundVirtualRegister final: public TVirtualRegister
{
    std::vector<PVirtualRegister> VirtualRegisters;

public:
    TCompoundVirtualRegister(std::vector<PVirtualRegister> && virtualRegisters);

    size_t GetHash() const noexcept override;
    bool operator==(const TVirtualRegister & rhs) const noexcept override;
    const PSerialDevice & GetDevice() const override;
    const TPSet<TProtocolRegister> & GetProtocolRegisters() const override;
    bool IsReady() const override;
    void ResetReady() override;
    EErrorState GetErrorState() const override;
    std::string GetTextValue() const override;
    void SetTextValue(const std::string & value) override;
    bool NeedToFlush() const override;
    bool Flush() override;
    bool NotifyRead(const PProtocolRegister &) override;
};
