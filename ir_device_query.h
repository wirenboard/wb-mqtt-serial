#pragma once

#include "declarations.h"
#include "utils.h"
#include "types.h"

#include <list>
#include <cassert>

/**
 * intermediate, protocol-agnostic device query representation format data structures
 */

struct TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPMap<PProtocolRegister, uint16_t> ProtocolRegisters;
    const TPUnorderedSet<PVirtualRegister>   VirtualRegisters;
    const bool                               HasHoles;
    const EQueryOperation                    Operation;

private:
    mutable EQueryStatus Status;

protected:
    explicit TIRDeviceQuery(const TPSet<PProtocolRegister> &, EQueryOperation = EQueryOperation::Read);

public:
    virtual ~TIRDeviceQuery() = default;

    bool operator<(const TIRDeviceQuery &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;
    void SetValuesFromDevice(const std::vector<uint64_t> & values) const;
    void SetStatus(EQueryStatus) const;
    EQueryStatus GetStatus() const;

    void SetEnabledWithRegisters(bool);
    bool IsEnabled() const;

    template <class T>
    const T & As() const
    {
        static_assert(std::is_base_of<TIRDeviceQuery, T>::value, "trying to cast to type not derived from TIRDeviceQuery");

        const T * pointer = dynamic_cast<const T *>(this);
        assert(pointer);
        return *pointer;
    }

    std::string Describe() const;
};

struct TIRDeviceValueQuery: TIRDeviceQuery
{
    virtual void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const = 0;
    virtual void SetValue(size_t index, uint64_t value) = 0;
    virtual uint64_t GetValue(size_t index) const = 0;

    void SetValue(const PProtocolRegister & reg, uint64_t value);
    void AcceptValues() const;

protected:
    TIRDeviceValueQuery(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation)
        : TIRDeviceQuery(registerSet, operation)
    {}
};

template <typename T>
struct TIRDeviceValueQueryImpl: TIRDeviceValueQuery
{
    friend class TIRDeviceQueryFactory;

    const std::vector<_mutable<T>> Values;

    void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const override
    {
        for (const auto & regIndex: ProtocolRegisters) {
            accessor(*regIndex.first, Values[regIndex.second].Value);
        }
    }

    void SetValue(size_t index, uint64_t value) override
    {
        assert(index < Values.size());
        Values[index] = value;
    }

    uint64_t GetValue(size_t index) const override
    {
        assert(index < Values.size());
        return Values[index];
    }

protected:
    explicit TIRDeviceValueQueryImpl(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation = EQueryOperation::Write)
        : TIRDeviceValueQuery(registerSet, operation)
        , Values(registerSet.size())
    {}
};

using TIRDevice64BitQuery = TIRDeviceValueQueryImpl<uint64_t>;
using TIRDeviceSingleBitQuery = TIRDeviceValueQueryImpl<bool>;


struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(std::list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
