#pragma once

#include "types.h"
#include "utils.h"
#include "declarations.h"


class TProtocolRegister
{
    friend TIRDeviceQuery;

    std::set<PWVirtualRegister, utils::weak_ptr_cmp<TVirtualRegister>> VirtualRegisters;

    PVirtualRegister AssociatedVirtualRegister() const;

public:
    uint32_t Address;
    uint32_t Type;

    TProtocolRegister(uint32_t address, uint32_t type);

    bool operator<(const TProtocolRegister &);

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &);

    std::set<TIntervalMs> GetPollIntervals() const;
    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;

private:
    void NotifyRead(const PProtocolRegister &);
};
