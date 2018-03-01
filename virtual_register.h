#pragma once

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
 * VirtualRegisters cannot be compared since they may overlap each other
 */
class TVirtualRegister final: public TAbstractVirtualRegister, public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    friend TProtocolRegister;

    PWVirtualRegisterSet                        VirtualRegisterSet;
    PWSerialDevice                              Device;
    TPMap<PProtocolRegister, TRegisterBindInfo> ProtocolRegisters;
    PBinarySemaphore                            FlushNeeded;
    std::atomic<uint64_t>                       ValueToWrite;
    uint64_t                                    CurrentValue;
    PIRDeviceValueQuery                         WriteQuery;
    EErrorState                                 ErrorState;
    EPublishData                                ChangedPublishData;
    uint8_t                                     ProtocolRegisterWidth;
    uint8_t                                     ReadRegistersCount;     // optimization
    bool                                        ValueWasAccepted;
    mutable bool                                IsRead;                 // optimization
    bool                                        Enabled;
    std::atomic_bool                            Dirty;

public:
    using TInitContext = std::map<std::pair<PSerialDevice, uint64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext &);

    /**
     * Build resulting value from protocol registers' values according to binding info
     */
    static uint64_t MapValueFrom(const TPSet<PProtocolRegister> &, const std::vector<TRegisterBindInfo> &);
    static uint64_t MapValueFrom(const TPMap<PProtocolRegister, TRegisterBindInfo> &);

    /**
     * Split given value and set to protocol registers according to binding info
     */
    static void MapValueTo(const PIRDeviceValueQuery &, const TPMap<PProtocolRegister, TRegisterBindInfo> &, uint64_t);

    /**
     * Returns hash that can be used by unordered_* containers
     */
    size_t GetHash() const noexcept;
    bool operator==(const TVirtualRegister & rhs) const noexcept;

    void SetFlushSignal(PBinarySemaphore flushNeeded);
    PSerialDevice GetDevice() const;
    TPSet<PProtocolRegister> GetProtocolRegisters() const;
    EErrorState GetErrorState() const;
    std::string GetTextValue() const;
    std::string Describe() const;
    void SetTextValue(const std::string & value);
    bool NeedToPoll() const;
    bool ValueIsRead() const;
    void InvalidateProtocolRegisterValues();
    bool IsChanged(EPublishData) const;
    bool NeedToFlush() const;
    bool Flush();

    void AssociateWithSet(const PVirtualRegisterSet & virtualRegisterSet);

    /**
     * Returns this if not associated with register set, otherwise returns associated register set
     */
    PAbstractVirtualRegister GetTopLevel();

    void NotifyPublished(EPublishData);

    uint64_t GetValue() const;

    bool IsEnabled() const;
    void SetEnabled(bool);

private:
    static void MapValueFromIteration(const PProtocolRegister &, const TRegisterBindInfo &, uint64_t &, uint8_t &);

    TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, TInitContext & context);

    uint64_t ComposeValue() const;

    bool AcceptDeviceValue(bool & changed, bool ok);

    uint32_t GetBitPosition() const;
    uint8_t GetBitSize() const;

    uint8_t GetUsedBitCount(const PProtocolRegister & reg) const;

    void AssociateWithProtocolRegisters();

    bool UpdateReadError(bool error);
    bool UpdateWriteError(bool error);

    bool NotifyRead(const PProtocolRegister &, bool ok);
    bool NotifyWrite(const PProtocolRegister &, bool ok);
};
