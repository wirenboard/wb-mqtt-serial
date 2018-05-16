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
    const TPSet<PVirtualRegister>   VirtualRegisters;    // registers that will be fully read or written after execution of query
    const bool                      HasHoles;
    const EQueryOperation           Operation;

private:
    mutable EQueryStatus Status;
    bool                 AbleToSplit;

protected:
    explicit TIRDeviceQuery(const TPSet<PMemoryBlock> &, EQueryOperation = EQueryOperation::Read);
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

    TPMap<PMemoryBlock, TIRDeviceMemoryBlockView> MemoryBlockValues;
    // NOTE: memory will be allocated also for unused memory blocks and holes
    std::vector<uint8_t>    Memory;
    TIRDeviceMemoryView     MemoryView;


    explicit TIRDeviceValueQuery(const TPSet<PMemoryBlock> & memoryBlockSet, EQueryOperation = EQueryOperation::Write);

    void IterRegisterValues(std::function<void(TMemoryBlock &, const TIRDeviceMemoryBlockView &)> && accessor) const;
    void SetValue(const TIRDeviceValueDesc & valueDesc, uint64_t value) const;

    template <typename T>
    void GetValues(T * values) const
    {
        CheckTypeMany<T>();

        GetValuesImpl(values, sizeof(T), GetValueCount());
    }

    template <typename T>
    std::vector<T> GetValues() const
    {
        CheckTypeMany<T>();

        std::vector<T> values;
        values.resize(GetValueCount());

        GetValuesImpl(values.data(), sizeof(T), values.size());

        return std::move(values);
    }

    /**
     * Accept written values to device as current and set status to Ok
     */
    void FinalizeWrite() const;

protected:
    void GetValuesImpl(void * mem, size_t size, size_t count) const;
};

struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(std::list<TPSet<PMemoryBlock>> && memoryBlockSets, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
