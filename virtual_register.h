#pragma once

#include "register_config.h"


using TProtocolRegisterSet = std::set<PProtocolRegister, utils::ptr_cmp<TProtocolRegister>>;

/**
 * Virtual register is a formatted top-level device data representation layer.
 * It translates data from user representation to protocol-specific register representation.
 * It can be associated with multiple ProtocolRegisters.
 * ProtocolRegisters don't have to be adjacent by addresses.
 * ProtocolRegisters have to be of same type (debatable)
 */
class TVirtualRegister: public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    PSerialDevice        Device;
    TProtocolRegisterSet ProtocolRegisters;
    uint8_t              ProtocolRegisterWidth;
    PIRDeviceWriteQuery  WriteQuery;

    explicit TVirtualRegister(const PRegisterConfig &);

    uint64_t GetBitPosition() const;
    uint64_t GetBitSize() const;
public:
    using TInitContext = std::map<std::pair<PSerialDevice, int64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext &);

    bool operator<(const TVirtualRegister & rhs) const noexcept;

    const PSerialDevice & GetDevice() const;
    const TProtocolRegisterSet & GetProtocolRegisters() const;

    void AssociateWith(const PProtocolRegister &);

};
