#pragma once

#include "declarations.h"
#include "utils.h"
#include "types.h"
#include "ir_device_memory_view.h"

#include <list>
#include <vector>
#include <cassert>

/**
 * Intermediate, protocol-agnostic device query representation format data structures
 */
struct TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPSetRange<PMemoryBlock>  MemoryBlockRange;
    const std::vector<PVirtualRegister>   VirtualRegisters;    // registers that will be fully read or written after execution of query
    const bool                      HasHoles;
    const EQueryOperation           Operation;

private:
    mutable EQueryStatus Status;
    bool                 AbleToSplit;

protected:
    explicit TIRDeviceQuery(std::vector<PVirtualRegister> &&, EQueryOperation = EQueryOperation::Read);
    void SetStatus(EQueryStatus) const;

    template <typename T>
    static void CheckTypeSingle()
    {
        static_assert(std::is_fundamental<T>::value, "only fundamental types are allowed");
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
    uint32_t GetBlockCount() const;
    uint32_t GetValueCount() const;
    uint32_t GetStart() const;
    uint16_t GetBlockSize() const;
    uint32_t GetSize() const;
    const TMemoryBlockType & GetType() const;
    const std::string & GetTypeName() const;

    void SetStatus(EQueryStatus);
    EQueryStatus GetStatus() const;
    void ResetStatus();
    void InvalidateReadValues();

    void SetEnabledWithRegisters(bool);
    bool IsEnabled() const;
    bool IsExecuted() const;
    bool IsAbleToSplit() const;
    void SetAbleToSplit(bool);

    TIRDeviceMemoryView CreateMemoryView(void * mem, size_t size) const;
    TIRDeviceMemoryView CreateMemoryView(const void * mem, size_t size) const;

    template <class T>
    const T & As() const
    {
        static_assert(std::is_base_of<TIRDeviceQuery, T>::value, "trying to cast to type not derived from TIRDeviceQuery");

        const T * pointer = dynamic_cast<const T *>(this);
        assert(pointer);
        return *pointer;
    }

    /**
     * Accept read memory from device as current and set status to Ok
     */
    void FinalizeRead(const void * mem, size_t size) const
    {
        FinalizeRead(CreateMemoryView(mem, size));
    }

    /**
     * Accept read memory from device as current and set status to Ok (dynamic array)
     */
    template <typename T>
    void FinalizeRead(const std::vector<T> & mem) const
    {
        CheckTypeMany<T>();

        FinalizeRead(CreateMemoryView(mem.data(), sizeof(T) * mem.size()));
    }

    /**
     * Accept read memory from device as current and set status to Ok (static array)
     */
    template <typename T, size_t N>
    void FinalizeRead(const T (& mem)[N]) const
    {
        CheckTypeMany<T>();

        FinalizeRead(CreateMemoryView(mem, sizeof(T) * N));
    }

    /**
     * Accept value read from device as current and set status to Ok (for single read to avoid unnecesary vector creation)
     */
    // template <typename T>
    // void FinalizeRead(T value) const
    // {
    //     CheckTypeSingle<T>();

    //     FinalizeRead(&value, sizeof(T));
    // }

    virtual void FinalizeRead(const TIRDeviceMemoryView &) const;

    std::string Describe() const;
    std::string DescribeOperation() const;
};

struct TIRDeviceValueQuery final: TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPSet<PMemoryBlock> MemoryBlocks;

    explicit TIRDeviceValueQuery(std::vector<PVirtualRegister> &&, EQueryOperation = EQueryOperation::Write);

    void SetValue(const TIRDeviceValueDesc & valueDesc, uint64_t value) const;

    TIRDeviceMemoryView GetValues(void * mem, size_t size) const
    {
        return GetValuesImpl(mem, size);
    }

    TIRDeviceMemoryView GetValues(std::vector<uint8_t> & values) const
    {
        values.resize(GetSize());

        return GetValuesImpl(values.data(), values.size());
    }

    /**
     * Accept written values to device as current and set status to Ok
     */
    void FinalizeWrite(const TIRDeviceMemoryView &) const;

protected:
    TIRDeviceMemoryView GetValuesImpl(void * mem, size_t size) const;
};

struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(const std::vector<PVirtualRegister> &, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
