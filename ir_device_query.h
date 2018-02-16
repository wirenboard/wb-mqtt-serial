#pragma once

#include "declarations.h"
#include "utils.h"

/**
 * intermediate, protocol-agnostic device query representation format data structures
 */

struct TIRDeviceQueryEntry
{
    // NOTE: DO NOT REORDER "Registers" AND "HasHoles" FIELDS
    const TPSet<TProtocolRegister> Registers;
    const bool                     HasHoles;
    mutable EQueryStatus           Status;

    TIRDeviceQueryEntry(TPSet<TProtocolRegister> &&);

    bool operator<(const TIRDeviceQueryEntry &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;
    bool NeedsSplit() const;

    std::string Describe() const;
};

template <typename T>
struct TIRDeviceValueQueryEntry: TIRDeviceQueryEntry
{
    const std::vector<T> Values;

    TIRDeviceValueQueryEntry(TPSet<TProtocolRegister> && registerSet)
        : TIRDeviceQueryEntry(std::move(registerSet))
        , Values(registerSet.size())
    {}
};

using TIRDevice64QueryEntry  = TIRDeviceValueQueryEntry<uint64_t>;
using TIRDeviceBitQueryEntry = TIRDeviceValueQueryEntry<bool>;

struct TIRDeviceQuery
{
    using Entry = TIRDeviceQueryEntry;
    using PEntry = std::shared_ptr<Entry>;

    TPSet<Entry> Entries;

    std::string Describe() const;
};

struct TIRDeviceReadQuery: TIRDeviceQuery
{
    using ActualEntry = TIRDeviceReadQueryEntry;

    PSerialDevice Device;

    TIRDeviceReadQuery(const TPSet<TProtocolRegister> & registers);

    void SplitIfNeeded();

private:
    static TPSet<Entry> GenerateEntries(const TPSet<TProtocolRegister> & registers, bool enableHoles);

    void Initialize(const TPSet<TProtocolRegister> & registers, bool enableHoles);
    void AddEntry(const PEntry &);
};

struct TIRDeviceWriteQuery: TIRDeviceQuery
{
    using ActualEntry = TIRDeviceWriteQueryEntry;
};
