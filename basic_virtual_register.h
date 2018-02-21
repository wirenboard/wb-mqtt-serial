#pragma once

#include "virtual_register.h"


class TBasicVirtualRegister final: public TVirtualRegister, public TRegisterConfig, public std::enable_shared_from_this<TBasicVirtualRegister>
{
    // /**
    //  * Continious memory block
    //  */
    // struct TBitRange
    // {
    //     uint32_t BitStart;
    //     uint32_t BitCount;

    //     bool operator<(const TBitRange & rhs) const noexcept;
    // };

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

    uint32_t GetBitPosition() const;
    uint32_t GetBitSize() const;

    void AssociateWith(const PProtocolRegister &, const TRegisterBindInfo &);

    bool UpdateReadError(bool error);
    bool UpdateWriteError(bool error);

public:
    TBasicVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context);

    /*-----interface implementation------*/
    size_t GetHash() const noexcept override;
    bool operator==(const TVirtualRegister & rhs) const noexcept override;

    const PSerialDevice & GetDevice() const override;
    const TPSet<TProtocolRegister> & GetProtocolRegisters() const override;
    EErrorState GetErrorState() const override;
    std::string GetTextValue() const override;
    const std::string & GetTypeName() const override;
    void SetTextValue(const std::string & value) override;
    bool NeedToPublish() const override;
    bool NeedToFlush() const override;
    bool Flush() override;
    bool AcceptDeviceValue(bool & changed) override;
    bool AcceptDeviceReadError() override;

    void NotifyPublished() override;
    bool NotifyRead(const PProtocolRegister &, bool ok) override;
    /*-----interface implementation------*/

    uint64_t GetValue() const;
};
