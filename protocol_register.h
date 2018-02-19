#pragma once

#include "types.h"
#include "utils.h"
#include "declarations.h"

/**
 * Each ProtocolRegister represents single actual register of device protocol.
 * This layer caches values and acts like bridge between VirtualRegister and IR layers.
 */
class TProtocolRegister: public std::enable_shared_from_this<TProtocolRegister>
{
    friend TIRDeviceValueQuery;
    friend TVirtualRegister;

    std::set<PWVirtualRegister, utils::weak_ptr_cmp<TVirtualRegister>> VirtualRegisters;
    uint64_t Value; //  most recent value of register (from successful writes and reads)

    PVirtualRegister AssociatedVirtualRegister() const;

public:
    using TRegisterCache = std::function<PProtocolRegister &(int)>; // address

    uint32_t Address;
    uint32_t Type;

    TProtocolRegister(uint32_t address, uint32_t type);

    static TPMap<TProtocolRegister, TRegisterBindInfo> GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device, TRegisterCache && = TRegisterCache());

    bool operator<(const TProtocolRegister &);

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &);

    std::set<TIntervalMs> GetPollIntervals() const;
    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPSet<TVirtualRegister> GetVirtualRegsiters() const;

private:
    void SetValueFromDevice(uint64_t value);
    void SetValueFromClient(uint64_t value);
};
