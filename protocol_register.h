#pragma once

#include "types.h"
#include "utils.h"
#include "declarations.h"

#include <iostream>

/**
 * Each ProtocolRegister represents single actual register of device protocol.
 * This layer caches values and acts like bridge between VirtualRegister and IR layers.
 * Protocol registers cannot overlap each other.
 */
class TProtocolRegister: public std::enable_shared_from_this<TProtocolRegister>
{
    friend TIRDeviceQuery;
    friend TVirtualRegister;

    TPWUnorderedSet<PWVirtualRegister> VirtualRegisters;

public:
    const uint32_t Address;
    const uint32_t Type;
private:
    // TODO: (optimization) move value to dedicated device-centric cache and store only shared (VirtualRegisters.size() > 1) protocol registers' values
    uint64_t Value; //  most recent value of register (from successful writes and reads)
    uint8_t  UsedBitCount;

public:
    TProtocolRegister(uint32_t address, uint32_t type);

    bool operator<(const TProtocolRegister &) const;
    bool operator==(const TProtocolRegister &) const;

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &);

    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPSet<PVirtualRegister> GetVirtualRegsiters() const;

    /**
     * Amount of bytes of protocol register that
     *  needs to be read to cover all required bits
     */
    uint8_t GetUsedByteCount() const;

    inline void SetValue(const uint64_t & value)
    {   Value = value;  std::cerr << " reg set val: " << value << std::endl; }

    inline const uint64_t & GetValue() const
    {   return Value;   }

    std::string Describe() const;

private:
    PVirtualRegister AssociatedVirtualRegister() const;
};
