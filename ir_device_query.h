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

    const TPSetView<PProtocolRegister> ProtocolRegistersView;
    const TPSet<PVirtualRegister>      VirtualRegisters;    // registers that will be fully read or written after execution of query
    const bool                         HasHoles;
    const EQueryOperation              Operation;

private:
    mutable EQueryStatus Status;
    bool                 AbleToSplit;

protected:
    explicit TIRDeviceQuery(const TPSet<PProtocolRegister> &, EQueryOperation = EQueryOperation::Read);
    void SetStatus(EQueryStatus) const;

    template <typename T>
    static void CheckTypeSingle()
    {
        static_assert(std::is_fundamental<T>::value, "only vector of fundamental types is allowed");
        static_assert(sizeof(T) <= sizeof(uint64_t), "size of type exceeded 64 bits");
    };

    template <typename T>
    static void CheckTypeMany()
    {
        CheckTypeSingle<T>();
        static_assert(!std::is_same<T, bool>::value, "vector<bool> is not supported");
    };

public:
    virtual ~TIRDeviceQuery() = default;

    bool operator<(const TIRDeviceQuery &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;

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

    /**
     * Accept values read from device as current and set status to Ok
     */
    template <typename T>
    void FinalizeRead(const void * values) const
    {
        CheckTypeMany<T>();

        FinalizeReadImpl(values, sizeof(T), GetCount());
    }

    /**
     * Accept values read from device as current and set status to Ok
     */
    template <typename T>
    void FinalizeRead(const std::vector<T> & values) const
    {
        CheckTypeMany<T>();

        FinalizeReadImpl(values.data(), sizeof(T), values.size());
    }

    /**
     * Accept value read from device as current and set status to Ok (for single read to avoid unnecesary vector creation)
     */
    template <typename T>
    void FinalizeRead(T value) const
    {
        CheckTypeSingle<T>();

        FinalizeReadImpl(&value, sizeof(T), 1);
    }

    std::string Describe() const;
    std::string DescribeOperation() const;

private:
    void FinalizeReadImpl(const void * mem, size_t size, size_t count) const;
};

struct TIRDeviceValueQuery: TIRDeviceQuery
{
    virtual void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const = 0;
    virtual void SetValue(const PProtocolRegister & reg, uint64_t value) const = 0;

    template <typename T>
    void GetValues(void * values) const
    {
        CheckTypeMany<T>();

        GetValuesImpl(values, sizeof(T), GetCount());
    }

    template <typename T>
    void GetValues(std::vector<T> & values) const
    {
        CheckTypeMany<T>();

        values.resize(GetCount());

        GetValuesImpl(values.data(), sizeof(T), values.size());
    }

    /**
     * Accept written values to device as current and set status to Ok
     */
    void FinalizeWrite() const;

protected:
    TIRDeviceValueQuery(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation)
        : TIRDeviceQuery(registerSet, operation)
    {}

    virtual void GetValuesImpl(void * mem, size_t size, size_t count) const = 0;
};

struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(std::list<TPSet<PProtocolRegister>> && registerSets, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
