#pragma once

#include "protocol_register_bind_info.h"
#include "abstract_virtual_register.h"
#include "register_config.h"
#include "utils.h"

#include <atomic>

/**
 * Virtual register is a formatted top-level device data representation layer.
 * It translates data from user representation to protocol-specific register representation.
 * It can be associated with multiple ProtocolRegisters.
 * ProtocolRegisters don't have to be adjacent by addresses.
 * ProtocolRegisters have to be of same type (debatable)
 * VirtualRegisters may share same protocol register
 */
class TVirtualRegister final: public TAbstractVirtualRegister, public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    friend TIRDeviceQuery;
    friend TIRDeviceValueQuery;

    PWVirtualRegisterSet                        VirtualRegisterSet;
    PWSerialDevice                              Device;
    TPMap<PProtocolRegister, TProtocolRegisterBindInfo> ProtocolRegisters;
    PBinarySemaphore                            FlushNeeded;
    std::atomic<uint64_t>                       ValueToWrite;
    uint64_t                                    CurrentValue;
    PIRDeviceValueQuery                         WriteQuery;
    EErrorState                                 ErrorState;
    EPublishData                                ChangedPublishData;
    std::atomic_bool                            Dirty;
    bool                                        Enabled : 1;
    bool                                        ValueIsRead : 1;
    bool                                        ValueWasAccepted : 1;

public:
    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device);

    /**
     * Build resulting value from protocol registers' values according to binding info
     */
    static uint64_t MapValueFrom(const TPMap<PProtocolRegister, TProtocolRegisterBindInfo> &);

    /**
     * Split given value and set to protocol registers according to binding info
     */
    static void MapValueTo(const PIRDeviceValueQuery &, const TPMap<PProtocolRegister, TProtocolRegisterBindInfo> &, uint64_t);

    /**
     * Returns hash that can be used by unordered_* containers
     */
    size_t GetHash() const noexcept;
    bool operator==(const TVirtualRegister & rhs) const noexcept;
    bool operator<(const TVirtualRegister & rhs) const noexcept;

    void SetFlushSignal(PBinarySemaphore flushNeeded);
    PSerialDevice GetDevice() const;
    TPSet<PProtocolRegister> GetProtocolRegisters() const;
    const TPMap<PProtocolRegister, TProtocolRegisterBindInfo> & GetBoundMemoryBlocks() const;
    EErrorState GetErrorState() const override;
    std::string Describe() const;
    bool NeedToPoll() const;
    bool GetValueIsRead() const override;
    bool IsChanged(EPublishData) const override;
    bool NeedToFlush() const;
    void Flush();

    void AssociateWithSet(const PVirtualRegisterSet & virtualRegisterSet);

    /**
     * Returns this if not associated with register set, otherwise returns associated register set
     */
    PAbstractVirtualRegister GetTopLevel();

    void ResetChanged(EPublishData) override;

    std::string GetTextValue() const override;
    uint64_t GetValue() const;

    void SetTextValue(const std::string & value) override;
    void SetValue(uint64_t value);

    bool IsEnabled() const;
    void SetEnabled(bool);

    std::string ToString() const;

    bool AreOverlapping(const TVirtualRegister & other) const;
    /**
     * Only for testing purposes
     */
    const PIRDeviceValueQuery & GetWriteQuery() const;
    void WriteValueToQuery();

private:
    TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device);
    void Initialize();

    uint64_t ComposeValue() const;

    void AcceptDeviceValue(bool ok);

    uint32_t GetBitPosition() const;
    uint32_t GetBitEnd() const;

    void UpdateReadError(bool error);
    void UpdateWriteError(bool error);

    void NotifyRead(bool ok);
    void NotifyWrite(bool ok);

    void InvalidateReadValues();
};
