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
class TMemoryBlock: public std::enable_shared_from_this<TMemoryBlock>
{
    friend TIRDeviceQuery;
    friend TVirtualRegister;
    friend TSerialDevice;

    uint8_t * TmpValue, // most recent value of register (from successful writes and reads)
            * Cache;    // on demand cache memory

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
    TMemoryBlock(uint32_t address, uint16_t size, const TMemoryBlockType & type);

    /**
     * Create with no external linkage
     */
    TMemoryBlock(uint32_t address, const TMemoryBlockType & type);

    /**
     * Create and link to device
     */
    TMemoryBlock(uint32_t address, uint16_t size, uint32_t typeIndex, const PSerialDevice &);

    /**
     * Create and link to device
     */
    TMemoryBlock(uint32_t address, uint32_t typeIndex, const PSerialDevice &);

public:
    bool operator<(const TMemoryBlock &) const;
    bool operator==(const TMemoryBlock &) const;

    void AssociateWith(const PVirtualRegister &);
    bool IsAssociatedWith(const PVirtualRegister &) const;
    bool IsReady() const;

    const std::string & GetTypeName() const;

    PSerialDevice GetDevice() const;
    TPSet<PVirtualRegister> GetVirtualRegsiters() const;

    void CacheIfNeeded(const uint8_t * data);
    uint8_t GetCachedByte(uint16_t index) const;

    std::string Describe() const;
};
