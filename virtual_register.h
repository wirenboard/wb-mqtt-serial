#pragma once

#include "register_config.h"
#include "utils.h"

#include <atomic>

/**
 * Virtual register is a formatted top-level device data representation layer.
 * It translates data from user representation to protocol-specific register representation.
 * It can be associated with multiple ProtocolRegisters.
 * ProtocolRegisters don't have to be adjacent by addresses.
 * ProtocolRegisters have to be of same type (debatable)
 * VirtualRegister cannot be compared since they may overlap each other
 */
class TVirtualRegister
{
    friend TProtocolRegister;

public:
    using TInitContext = std::map<std::pair<PSerialDevice, uint64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const std::vector<PRegisterConfig> & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext &);

    /**
     * Build resulting value from protocol registers' values according to binding info
     */
    static uint64_t MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> &);

    /**
     * Split given value and set to protocol registers according to binding info
     */
    static void MapValueTo(const TPMap<TProtocolRegister, TRegisterBindInfo> &, uint64_t);

    virtual ~TVirtualRegister() = default;

    /**
     * Returns hash that can be used by unordered_* containers
     */
    virtual size_t GetHash() const noexcept = 0;
    virtual bool operator==(const TVirtualRegister & rhs) const noexcept = 0;
    virtual const PSerialDevice & GetDevice() const = 0;
    virtual const TPSet<TProtocolRegister> & GetProtocolRegisters() const = 0;
    virtual EErrorState GetErrorState() const = 0;
    virtual std::string GetTextValue() const = 0;
    virtual const std::string & GetTypeName() const = 0;
    virtual void SetTextValue(const std::string & value) = 0;
    virtual bool NeedToPublish() const = 0;
    virtual bool NeedToFlush() const = 0;
    virtual bool Flush() = 0;

    virtual bool AcceptDeviceValue(bool & changed) = 0;
    virtual bool AcceptDeviceReadError() = 0;

    virtual void NotifyPublished() = 0;

protected:
    virtual bool NotifyRead(const PProtocolRegister &, bool ok) = 0;
};
