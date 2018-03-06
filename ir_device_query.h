#pragma once

#include "declarations.h"
#include "utils.h"
#include "types.h"

#include <list>
#include <cassert>

/**
 * Intermediate, protocol-agnostic device query representation format data structures
 */
struct TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPMap<PProtocolRegister, uint16_t> ProtocolRegisters;
    const TPUnorderedSet<PVirtualRegister>   VirtualRegisters;    // registers that will be fully read or written after execution of query
    const bool                               HasHoles;
    const EQueryOperation                    Operation;

private:
    mutable EQueryStatus Status;
    bool                 AbleToSplit;

protected:
    explicit TIRDeviceQuery(const TPSet<PProtocolRegister> &, EQueryOperation = EQueryOperation::Read);
    void SetStatus(EQueryStatus) const;

public:
    virtual ~TIRDeviceQuery() = default;

    bool operator<(const TIRDeviceQuery &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;

    /**
     * Accept values read from device as current and set status to Ok
     */
    void FinalizeRead(const std::vector<uint64_t> & values) const;

    /**
     * Accept value read from device as current and set status to Ok (for single read to avoid unnecesary vector creation)
     */
    void FinalizeRead(const uint64_t & value) const;

    void SetStatus(EQueryStatus);
    EQueryStatus GetStatus() const;
    void ResetStatus();

    void SetEnabledWithRegisters(bool);
    bool IsEnabled() const;
    bool IsExecuted() const;
    bool IsAbleToSplit() const;
    void SetAbleToSplit(bool);

    template <class T>
    const T & As() const
    {
        static_assert(std::is_base_of<TIRDeviceQuery, T>::value, "trying to cast to type not derived from TIRDeviceQuery");

        const T * pointer = dynamic_cast<const T *>(this);
        assert(pointer);
        return *pointer;
    }

    std::string Describe() const;
    std::string DescribeOperation() const;
};

struct TIRDeviceValueQuery: TIRDeviceQuery
{
    virtual void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const = 0;
    virtual void SetValue(size_t index, uint64_t value) const = 0;
    virtual uint64_t GetValue(size_t index) const = 0;

    void SetValue(const PProtocolRegister & reg, uint64_t value) const;

    /**
     * Accept written values to device as current and set status to Ok
     */
    void FinalizeWrite() const;

protected:
    TIRDeviceValueQuery(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation)
        : TIRDeviceQuery(registerSet, operation)
    {}
};

struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(std::list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
