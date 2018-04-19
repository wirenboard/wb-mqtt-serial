#pragma once

#include "types.h"
#include "utils.h"
#include "declarations.h"

/**
 * @brief: Lightweight representation of atomic chunk of memory accessed via device protocol,
 *   which means that any part of memory block can't be obtained without obtaining whole block.
 *  This layer caches values and acts like bridge between VirtualRegister and IR layers.
 *  Different memory blocks cannot simultaneously have same type and address.
 */
class TProtocolRegister: public std::enable_shared_from_this<TProtocolRegister>
{
    friend TIRDeviceQuery;
    friend TVirtualRegister;
    friend TSerialDevice;

    uint8_t * Cache; //  most recent value of register (from successful writes and reads)

public:
    const uint32_t           Address;
    const TMemoryBlockType & Type;
    const uint16_t           Size;

private:
    struct IExternalLinkage
    {
        virtual ~IExternalLinkage() = default;
        virtual PSerialDevice GetDevice() const = 0;
        virtual TPSet<PVirtualRegister> GetVirtualRegsiters() const = 0;
        virtual void LinkWith(const PVirtualRegister &) = 0;
        virtual bool IsLinkedWith(const PVirtualRegister &) const = 0;
        virtual bool NeedsCaching() const = 0;
    };

    std::unique_ptr<IExternalLinkage> ExternalLinkage;

    bool InitExternalLinkage(const PSerialDevice &);
    bool InitExternalLinkage(const PVirtualRegister &);

    /**
     * Create with no external linkage
     */
    TProtocolRegister(uint32_t address, uint16_t size, const TMemoryBlockType & type);

    /**
     * Create with no external linkage
     */
    TProtocolRegister(uint32_t address, const TMemoryBlockType & type);

    /**
     * Create and link to device
     */
    TProtocolRegister(uint32_t address, uint16_t size, uint32_t typeIndex, const PSerialDevice &);

    /**
     * Create and link to device
     */
    TProtocolRegister(uint32_t address, uint32_t typeIndex, const PSerialDevice &);

public:
    bool operator<(const TProtocolRegister &) const;
    bool operator==(const TProtocolRegister &) const;

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &) const;
    bool IsReady() const;

    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPSet<PVirtualRegister> GetVirtualRegsiters() const;

    inline void SetValue(const uint64_t & value)
    {   Value = value; }

    inline const uint64_t & GetValue() const
    {   return Value;   }

    std::string Describe() const;
};
