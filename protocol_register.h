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
    friend TVirtualRegister;

    bool InitExternalLinkage(const PSerialDevice &);
    bool InitExternalLinkage(const PVirtualRegister &);

public:
    const uint32_t Address;
    const uint32_t Type;

private:
    // TODO: (optimization) move value to dedicated device-centric cache and store only shared (VirtualRegisters.size() > 1) protocol registers' values
    uint64_t Value; //  most recent value of register (from successful writes and reads)

    struct IExternalLinkage
    {
        virtual ~IExternalLinkage() = default;
        virtual PSerialDevice GetDevice() const = 0;
        virtual TPSet<PVirtualRegister> GetVirtualRegsiters() const = 0;
        virtual void LinkWith(const PProtocolRegister &, const PVirtualRegister &) = 0;
        virtual bool IsLinkedWith(const PVirtualRegister &) const = 0;
        virtual uint8_t GetUsedByteCount() const = 0;
        virtual const std::string & GetTypeName() const = 0;
    };

    std::unique_ptr<IExternalLinkage> ExternalLinkage;

public:
    /**
     * Create with no external linkage
     */
    TProtocolRegister(uint32_t address, uint32_t type);

    /**
     * Create and link to device
     */
    TProtocolRegister(uint32_t address, uint32_t type, const PSerialDevice &);

    bool operator<(const TProtocolRegister &) const;
    bool operator==(const TProtocolRegister &) const;

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &) const;
    bool IsReady() const;

    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPSet<PVirtualRegister> GetVirtualRegsiters() const;

    /**
     * Amount of bytes of protocol register that
     *  needs to be read to cover all required bits
     */
    uint8_t GetUsedByteCount() const;

    inline void SetValue(const uint64_t & value)
    {   Value = value; }

    inline const uint64_t & GetValue() const
    {   return Value;   }

    std::string Describe() const;
};
