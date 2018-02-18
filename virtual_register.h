#pragma once

#include "register_config.h"

/**
 * Virtual register is a formatted top-level device data representation layer.
 * It translates data from user representation to protocol-specific register representation.
 * It can be associated with multiple ProtocolRegisters.
 * ProtocolRegisters don't have to be adjacent by addresses.
 * ProtocolRegisters have to be of same type (debatable)
 */
class TVirtualRegister: public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    friend TProtocolRegister;

    PSerialDevice                   Device;
    TPMap<TProtocolRegister, bool>  ProtocolRegisters;
    uint8_t                         ProtocolRegisterWidth;
    PIRDeviceQuerySet  WriteQuery;

    explicit TVirtualRegister(const PRegisterConfig &);

    uint32_t GetBitPosition() const;
    uint32_t GetBitSize() const;

public:
    using TInitContext = std::map<std::pair<PSerialDevice, int64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext &);

    bool operator<(const TVirtualRegister & rhs) const noexcept;

    const PSerialDevice & GetDevice() const;
    const TPSet<TProtocolRegister> & GetProtocolRegisters() const;

    void AssociateWith(const PProtocolRegister &);

private:
    void NotifyRead(const PProtocolRegister &);
};
