#pragma once

#include "declarations.h"

#include <vector>


class TVirtualRegisterSet
{
    std::vector<PVirtualRegister> VirtualRegisters;

public:
    TVirtualRegisterSet(std::vector<PVirtualRegister> &&);

    std::string GetTextValue() const;
    void SetTextValue(const std::string &);
};
