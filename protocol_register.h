#pragma once

#include "types.h"
#include "utils.h"
#include "declarations.h"

/**
 * Each ProtocolRegister represents single actual register of device protocol.
 * This layer caches values and acts like bridge between VirtualRegister and IR layers.
 * Protocol registers cannot overlap each other.
 */
class TProtocolRegister: public std::enable_shared_from_this<TProtocolRegister>
{
    friend TIRDeviceQuery;
    friend TIRDeviceValueQuery;
    friend TVirtualRegister;

    TPWUnorderedSet<PWVirtualRegister> VirtualRegisters;
    uint64_t Value; //  most recent value of register (from successful writes and reads)
    bool     Enabled;

    PVirtualRegister AssociatedVirtualRegister() const;

    void DisableIfNotUsed();

public:
    using TRegisterCache = std::function<PProtocolRegister &(int)>; // address

    const uint32_t Address;
    const uint32_t Type;

    TProtocolRegister(uint32_t address, uint32_t type);

    static TPMap<PProtocolRegister, TRegisterBindInfo> GenerateProtocolRegisters(const PRegisterConfig & config, const PSerialDevice & device, TRegisterCache && = TRegisterCache());

    bool operator<(const TProtocolRegister &) const;
    bool operator==(const TProtocolRegister &) const;

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &);

    std::set<TIntervalMs> GetPollIntervals() const;
    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPUnorderedSet<PVirtualRegister> GetVirtualRegsiters() const;
    uint8_t GetUsedByteCount() const;

    void SetReadValue(uint64_t value);
    void SetWriteValue(uint64_t value);
    void SetReadError(bool error = true);
    void SetWriteError(bool error = true);

    bool IsEnabled() const;
};
