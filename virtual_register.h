#pragma once

#include "register_config.h"

#include <atomic>

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

    PSerialDevice                               Device;
    TPMap<TProtocolRegister, TRegisterBindInfo> ProtocolRegisters;
    uint8_t                                     ProtocolRegisterWidth;
    PBinarySemaphore                            FlushNeeded;
    std::atomic_bool                            Dirty;
    std::atomic<uint64_t>                       ValueToWrite;
    bool                                        ValueWasAccepted;
    uint64_t                                    ReadValue;
    PIRDeviceQuerySet                           WriteQuerySet;
    EErrorState                                 ErrorState;

    explicit TVirtualRegister(const PRegisterConfig &, const PSerialDevice &, PBinarySemaphore flushNeeded);

    uint32_t GetBitPosition() const;
    uint32_t GetBitSize() const;

    void AssociateWith(const PProtocolRegister &, const TRegisterBindInfo &);

    bool UpdateReadError(bool error);
    bool UpdateWriteError(bool error);
    bool AcceptDeviceValue(bool * changed = nullptr);
    bool AcceptDeviceReadError();

public:
    using TInitContext = std::map<std::pair<PSerialDevice, uint64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext &);

    static uint64_t MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> &);
    static void MapValueTo(const TPMap<TProtocolRegister, TRegisterBindInfo> &, uint64_t);

    bool operator<(const TVirtualRegister & rhs) const noexcept;

    const PSerialDevice & GetDevice() const;
    const TPSet<TProtocolRegister> & GetProtocolRegisters() const;
    bool IsReady() const;
    void ResetReady();
    EErrorState GetErrorState() const;
    uint64_t GetValue() const;
    std::string GetTextValue() const;
    void SetTextValue(const std::string & value);

    bool NeedToFlush() const;

    bool Flush();

private:
    void NotifyRead(const PProtocolRegister &);
};
