#pragma once

#include "declarations.h"
#include "utils.h"

/**
 * intermediate, protocol-agnostic device query representation format data structures
 */

struct TIRDeviceQuery
{
    // NOTE: DO NOT REORDER "Registers" AND "HasHoles" FIELDS
    const TPSet<TProtocolRegister> Registers;
    const bool                     HasHoles;
    mutable EQueryStatus           Status;

    TIRDeviceQuery(TPSet<TProtocolRegister> &&);
    virtual ~TIRDeviceQuery() = default;

    bool operator<(const TIRDeviceQuery &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;
    bool NeedsSplit() const;

    virtual void IterRegisterValues(std::function<bool(const TProtocolRegister &, uint64_t)> && accessor) const = 0;
    virtual void AcceptValues() const = 0;

    std::string Describe() const;
};

template <typename T>
struct TIRDeviceValueQuery: TIRDeviceQuery
{
    const std::vector<_mutable<T>> Values;

    TIRDeviceValueQuery(TPSet<TProtocolRegister> && registerSet)
        : TIRDeviceQuery(std::move(registerSet))
        , Values(registerSet.size())
    {}

    void IterRegisterValues(std::function<bool(const TProtocolRegister &, uint64_t)> && accessor) const override
    {
        uint32_t i = 0;
        for (const auto & protocolRegister: Registers) {
            if (accessor(*protocolRegister, Values[i++].Value)) {
                return;
            }
        }
    }
};

struct TIRDevice64BitQuery: TIRDeviceValueQuery<uint64_t> {};
struct TIRDeviceSingleBitQuery: TIRDeviceValueQuery<bool> {};


struct TIRDeviceQuerySet
{
    TPSet<TIRDeviceQuery> Queries;

    TIRDeviceQuerySet(const TPSet<TProtocolRegister> & registers);

    std::string Describe() const;
    PSerialDevice GetDevice() const;

    void SplitIfNeeded();

private:
    static TPSet<TIRDeviceQuery> GenerateQueries(const TPSet<TProtocolRegister> & registers, bool enableHoles);

    void Initialize(const TPSet<TProtocolRegister> & registers, bool enableHoles);
};
