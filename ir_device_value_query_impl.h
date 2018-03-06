#pragma once

#include "ir_device_query.h"

template <typename T>
struct TIRDeviceValueQueryImpl final: TIRDeviceValueQuery
{
    friend class TIRDeviceQueryFactory;

    const std::vector<_mutable<T>> Values;

    void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const override
    {
        for (const auto & regIndex: ProtocolRegisters) {
            accessor(*regIndex.first, Values[regIndex.second].Value);
        }
    }

    void SetValue(size_t index, uint64_t value) const override
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
