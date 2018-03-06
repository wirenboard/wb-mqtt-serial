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
 * VirtualRegisters cannot be compared since they may overlap each other
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
    uint8_t                                     ProtocolRegisterWidth;
    std::atomic_bool                            Dirty;
    bool                                        Enabled : 1;
    bool                                        ValueIsRead : 1;
    bool                                        ValueWasAccepted : 1;

public:
    using TInitContext = std::map<std::pair<PSerialDevice, uint64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, TInitContext &);

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

    void SetFlushSignal(PBinarySemaphore flushNeeded);
    PSerialDevice GetDevice() const;
    TPSet<PProtocolRegister> GetProtocolRegisters() const;
    EErrorState GetErrorState() const;
    std::string GetTextValue() const;
    std::string Describe() const;
    void SetTextValue(const std::string & value);
    bool NeedToPoll() const;
    bool GetValueIsRead() const;
    void InvalidateProtocolRegisterValues();
    bool IsChanged(EPublishData) const;
    bool NeedToFlush() const;
    void Flush();

    void AssociateWithSet(const PVirtualRegisterSet & virtualRegisterSet);

    /**
     * Returns this if not associated with register set, otherwise returns associated register set
     */
    PAbstractVirtualRegister GetTopLevel();

    void NotifyPublished(EPublishData);

    uint64_t GetValue() const;

    bool IsEnabled() const;
    void SetEnabled(bool);

    const TProtocolRegisterBindInfo & GetBindInfo(const PProtocolRegister & reg) const;

private:
    TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, TInitContext & context);

    uint64_t ComposeValue() const;

    void AcceptDeviceValue(bool ok);

    uint32_t GetBitPosition() const;
    uint8_t GetBitSize() const;

    void AssociateWithProtocolRegisters();

    void UpdateReadError(bool error);
    void UpdateWriteError(bool error);

    void NotifyRead(bool ok);
    void NotifyWrite(bool ok);
};
