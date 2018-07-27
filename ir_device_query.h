#pragma once

#include "declarations.h"
#include "utils.h"
#include "types.h"
#include "ir_device_memory_view.h"

#include <list>
#include <vector>
#include <cassert>

/**
 * @brief intermediate, protocol-agnostic device query object
 */
struct TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPSetRange<PMemoryBlock>   MemoryBlockRange;
    // registers or setup items that will be fully read or written after execution of query
    const std::vector<PVirtualValue> VirtualValues;
    const bool                       HasHoles;
    const bool                       HasVirtualRegisters;
    const EQueryOperation            Operation;

private:
    mutable EQueryStatus Status;
    bool                 AbleToSplit;
    bool                 Enabled;

protected:
    /**
     * @brief create query with binding to virtual values.
     *  It'll update virtual registers values on finalize and
     *  maintain memory blocks cache in correct state as side effect.
     */
    explicit TIRDeviceQuery(TAssociatedMemoryBlockSet &&, EQueryOperation = EQueryOperation::Read);

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
    /**
     * @brief get device to which this query belongs
     *
     * @return PSerialDevice
     */
    PSerialDevice GetDevice() const;
    /**
     * @brief get number of memory blocks (including holes
     *  and unused memory blocks)
     *
     * @return uint32_t
     */
    uint32_t GetBlockCount() const;
    /**
     * @brief get number of protocol values that is
     *  expected to be acquired as response to this query.
     *
     * @return uint32_t
     */
    uint32_t GetValueCount() const;
    /**
     * @brief Starting address at range of addresses
     *
     * @return uint32_t
     */
    uint32_t GetStart() const;
    /**
     * @brief size of blocks
     *
     * @return TValueSize
     */
    TValueSize GetBlockSize() const;
    uint32_t GetSize() const;
    const TMemoryBlockType & GetType() const;
    const std::string & GetTypeName() const;

    /**
     * @brief set status of query execution
     *
     * @note this is set by device automatically on finalization
     *  of query or on exceptions during query execution.
     */
    void SetStatus(EQueryStatus) const;
    /**
     * @brief get status of query
     *
     * @return EQueryStatus
     */
    EQueryStatus GetStatus() const;
    /**
     * @brief set status of query to NotExecuted
     */
    void ResetStatus();
    /**
     * @brief invalidate read values of all
     *  virtual values affected by this query
     *
     * @note called at end of cycle
     */
    void InvalidateReadValues();

    /**
     * @brief used to set enabled all affected virtual registers
     */
    void SetEnabled(bool);
    /**
     * @brief returns true if there's any enabled virtual register
     *  affected by this query
     */
    bool IsEnabled() const;
    /**
     * @brief returns true if query was executed by device,
     *  successfully or not
     */
    bool IsExecuted() const;
    /**
     * @brief indicates ability of this query to split into
     *  multiple lesser queries
     */
    bool IsAbleToSplit() const;
    /**
     * @brief used to set ability to split externally
     *
     * @note we cannot say for sure wether or not we able to split
     *  query because query distribution logic is located at TIRDeviceQueryFactory
     *  so in case of redistribution attempt failure, we manually mark that
     *  query as not able to split to avoid pointless retries.
     */
    void SetAbleToSplit(bool);

    /**
     * @brief create view to passed memory according to query's data layout
     */
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

    virtual void FinalizeRead(const TIRDeviceMemoryView &) const;

    std::string Describe() const;
    std::string DescribeVerbose() const;
    std::string DescribeOperation() const;
};

/**
 * @brief device query that holds values.
 *  Used to write values to devices.
 */
struct TIRDeviceValueQuery final: TIRDeviceQuery
{
    friend class TIRDeviceQueryFactory;

    const TPSet<PMemoryBlock> MemoryBlocks; // needed only for mobdus write single multi funcionality

    explicit TIRDeviceValueQuery(TAssociatedMemoryBlockSet &&, EQueryOperation = EQueryOperation::Write);

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
     * @brief Accept written values to device, update cache and set status to Ok
     */
    void FinalizeWrite() const;

private:
    TIRDeviceMemoryView GetValuesImpl(void * mem, size_t size) const;
};

/**
 * @brief set of device queries.
 *
 * @note when creating set for each poll interval,
 *  eases dynamic query subdivision on errors,
 *  by allowing to modify query set instead of polling plan
 */
struct TIRDeviceQuerySet
{
    friend class TIRDeviceQuerySetHandler;

    TQueries Queries;

    TIRDeviceQuerySet(const std::vector<PVirtualRegister> &, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
