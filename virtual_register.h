#pragma once

#include "ir_bind_info.h"
#include "virtual_value.h"
#include "abstract_virtual_register.h"
#include "register_config.h"
#include "utils.h"

#include <atomic>


/**
 * Virtual register is a formatted top-level device data representation layer.
 * It translates data from user representation to protocol-specific register representation.
 * It can be associated with multiple MemoryBlocks.
 * MemoryBlocks don't have to be adjacent by addresses.
 * MemoryBlocks have to be of same type (debatable)
 * VirtualRegisters may share same protocol register
 */
class TVirtualRegister final: public IVirtualValue, public TAbstractVirtualRegister, public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    PWVirtualRegisterSet                        VirtualRegisterSet;
    PWSerialDevice                              Device;
    TIRDeviceValueDesc                          ValueDesc;
    PBinarySemaphore                            FlushNeeded;
    PIRValue                                    ValueToWrite;   // WAS ATOMIC
    PIRValue                                    CurrentValue;
    PIRDeviceValueQuery                         WriteQuery;
    EErrorState                                 ErrorState;
    EPublishData                                ChangedPublishData;
    std::atomic_bool                            Dirty;
    bool                                        ValueIsRead;
    bool                                        ValueWasAccepted;

    TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device);
    void Initialize();

public:
    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device);
    ~TVirtualRegister();

    /**
     * Returns hash that can be used by unordered_* containers
     */
    size_t GetHash() const noexcept;
    bool operator==(const TVirtualRegister & rhs) const noexcept;
    bool operator<(const TVirtualRegister & rhs) const noexcept;

    void SetFlushSignal(PBinarySemaphore flushNeeded);
    PSerialDevice GetDevice() const;

    TPSet<PMemoryBlock> GetMemoryBlocks() const;
    const TIRBindInfo & GetMemoryBlockBindInfo(const PMemoryBlock &) const;
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
    PAbstractVirtualRegister GetRoot();

    void ResetChanged(EPublishData) override;

    std::string GetTextValue() const override;
    void SetTextValue(const std::string & value) override;

    std::string GetTextValueToWrite() const;

    std::string ToString() const;

    bool AreOverlapping(const TVirtualRegister & other) const;
    /**
     * Only for testing purposes
     */
    const PIRDeviceValueQuery & GetWriteQuery() const;

    void AcceptDeviceValue();
    void UpdateReadError(bool error);

private:
    void AcceptWriteValue();
    void UpdateWriteError(bool error);

    TIRDeviceValueContext GetReadContext() const override;
    TIRDeviceValueContext GetWriteContext() const override;

    void InvalidateReadValues() override;

    uint64_t GetBitPosition() const;
};
