#pragma once

#include "declarations.h"
#include "utils.h"

/**
 * intermediate, protocol-agnostic device query representation format data structures
 */

struct TIRDeviceQuery
{
    // NOTE: DO NOT REORDER "Registers" , "VirtualRegisters" AND "HasHoles" FIELDS
    const TPSet<TProtocolRegister> Registers;
    const TPSet<TVirtualRegister>  VirtualRegisters;
    const bool                     HasHoles;
    const EQueryOperation          Operation;

private:
    mutable EQueryStatus           Status;

public:
    TIRDeviceQuery(TPSet<TProtocolRegister> &&);
    virtual ~TIRDeviceQuery() = default;

    bool operator<(const TIRDeviceQuery &) const noexcept;

    PSerialDevice GetDevice() const;
    uint32_t GetCount() const;
    uint32_t GetStart() const;
    uint32_t GetType() const;
    const std::string & GetTypeName() const;
    bool NeedsSplit() const;
    void SetValuesFromDevice(const std::vector<uint64_t> & values) const;
    const TPSet<TVirtualRegister> & GetAffectedVirtualRegisters() const;
    void SetStatus(EQueryStatus) const;
    EQueryStatus GetStatus() const;

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
    void AcceptValues() const;
};

template <typename T>
struct TIRDeviceValueQueryImpl: TIRDeviceValueQuery
{
    const std::vector<_mutable<T>> Values;

    TIRDeviceValueQuery(TPSet<TProtocolRegister> && registerSet)
        : TIRDeviceQuery(std::move(registerSet))
        , Values(registerSet.size())
    {
        Operation = EQueryOperation::WRITE;
    }

    void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const override
    {
        uint32_t i = 0;
        for (const auto & protocolRegister: Registers) {
            accessor(*protocolRegister, Values[i++].Value);
        }
    }
};

struct TIRDevice64BitQuery: TIRDeviceValueQueryImpl<uint64_t> {};
struct TIRDeviceSingleBitQuery: TIRDeviceValueQueryImpl<bool> {};


struct TIRDeviceQuerySet
{
    TPSet<TIRDeviceQuery> Queries;

    TIRDeviceQuerySet(const TPSet<TProtocolRegister> & registers, EQueryOperation);

    std::string Describe() const;
    PSerialDevice GetDevice() const;

    void SplitIfNeeded();

private:
    static TPSet<TIRDeviceQuery> GenerateQueries(const TPSet<TProtocolRegister> & registers, bool enableHoles, EQueryOperation);
};
