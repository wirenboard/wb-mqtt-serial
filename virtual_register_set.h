#pragma once

#include "declarations.h"
#include "abstract_virtual_register.h"

#include <vector>


class TVirtualRegisterSet final: public TAbstractVirtualRegister
{
    std::vector<PVirtualRegister> VirtualRegisters;

public:
    TVirtualRegisterSet(const std::vector<PVirtualRegister> &);

    std::string GetTextValue() const override;
    void SetTextValue(const std::string &) override;
    EErrorState GetErrorState() const override;
    bool GetValueIsRead() const override;
    bool IsChanged(EPublishData) const override;
};
