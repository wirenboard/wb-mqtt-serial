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
class TVirtualRegister final: public TRegisterConfig, public std::enable_shared_from_this<TVirtualRegister>
{
    friend TProtocolRegister;
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
    PIRDeviceValueQuery                         WriteQuery;
    EErrorState                                 ErrorState;

    static void MapValueFromIteration(const PProtocolRegister &, const TRegisterBindInfo &, uint64_t &, uint8_t &);

    TVirtualRegister(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext & context);

    uint32_t GetBitPosition() const;
    uint32_t GetBitSize() const;

    void AssociateWithProtocolRegisters();

    bool UpdateReadError(bool error);
    bool UpdateWriteError(bool error);

public:
    using TInitContext = std::map<std::pair<PSerialDevice, uint64_t>, PProtocolRegister>;

    static PVirtualRegister Create(const PRegisterConfig & config, const PSerialDevice & device, PBinarySemaphore flushNeeded, TInitContext &);

    /**
     * Build resulting value from protocol registers' values according to binding info
     */
    static uint64_t MapValueFrom(const TPSet<TProtocolRegister> &, const std::vector<TRegisterBindInfo> &);
    static uint64_t MapValueFrom(const TPMap<TProtocolRegister, TRegisterBindInfo> &);

    /**
     * Split given value and set to protocol registers according to binding info
     */
    static void MapValueTo(const PIRDeviceValueQuery &, const TPMap<TProtocolRegister, TRegisterBindInfo> &, uint64_t);

    virtual ~TVirtualRegister() = default;

    /**
     * Returns hash that can be used by unordered_* containers
     */
    size_t GetHash() const noexcept;
    bool operator==(const TVirtualRegister & rhs) const noexcept;

    const PSerialDevice & GetDevice() const;
    const TPSet<TProtocolRegister> & GetProtocolRegisters() const;
    EErrorState GetErrorState() const;
    std::string GetTextValue() const;
    const std::string & GetTypeName() const;
    void SetTextValue(const std::string & value);
    bool NeedToPublish() const;
    bool NeedToFlush() const;
    bool Flush();
    bool AcceptDeviceValue(bool & changed);
    bool AcceptDeviceReadError();

    void NotifyPublished();

    uint64_t GetValue() const;

private:
    bool NotifyRead(const PProtocolRegister &, bool ok);
};
