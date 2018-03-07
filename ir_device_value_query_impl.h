#pragma once

#include "ir_device_query.h"

#include <cstring>

template <typename T>
struct TIRDeviceValueQueryImpl final: TIRDeviceValueQuery
{
    friend class TIRDeviceQueryFactory;

    static_assert(std::is_fundamental<T>::value, "query can store only values of fundamental types");

    const TPMap<PProtocolRegister, _mutable<T>> ProtocolRegisterValues;


    void IterRegisterValues(std::function<void(TProtocolRegister &, uint64_t)> && accessor) const override
    {
        for (const auto & registerValue: ProtocolRegisterValues) {
            accessor(*registerValue.first, registerValue.second);
        }
    }

    void SetValue(const PProtocolRegister & reg, uint64_t value) const override
    {
        auto itRegisterValue = ProtocolRegisterValues.find(reg);
        assert(itRegisterValue != ProtocolRegisterValues.end());

        itRegisterValue->second = value;
    }

    void GetValuesImpl(void * mem, size_t size, size_t count) const override
    {
        assert(size <= sizeof(T));
        assert(GetCount() == count);

        auto itProtocolRegister = ProtocolRegistersView.Begin;
        auto itProtocolRegisterValue = ProtocolRegisterValues.begin();
        auto bytes = static_cast<uint8_t*>(mem);

        assert(*itProtocolRegister == itProtocolRegisterValue->first);

        for (size_t i = 0; i < count; ++i) {
            const auto requestedRegisterAddress = GetStart() + i;

            assert(itProtocolRegister != ProtocolRegistersView.End);
            assert(itProtocolRegisterValue != ProtocolRegisterValues.end());

            // try read value from query itself
            {
                const auto & protocolRegister = itProtocolRegisterValue->first;
                const auto & value = itProtocolRegisterValue->second;

                if (protocolRegister->Address == requestedRegisterAddress) {    // this register exists and query has its value - write from query
                    memcpy(bytes, &value, size);

                    ++itProtocolRegister;
                    ++itProtocolRegisterValue;
                    bytes += size;
                    continue;
                }
            }

            // try read value from cache
            {
                const auto & protocolRegister = *itProtocolRegister;

                if (protocolRegister->Address == requestedRegisterAddress) {    // this register exists but query doesn't have value for it - write cached value
                    memcpy(bytes, &protocolRegister->GetValue(), size);

                    ++itProtocolRegister;
                    bytes += size;
                    continue;
                }
            }

            // driver doesn't use this address (hole) - fill with zeroes
            {
                memset(bytes, size, 0);
                bytes += size;
            }
        }

        assert(itProtocolRegister == ProtocolRegistersView.End);
        assert(itProtocolRegisterValue == ProtocolRegisterValues.end());
    }

protected:
    explicit TIRDeviceValueQueryImpl(const TPSet<PProtocolRegister> & registerSet, EQueryOperation operation = EQueryOperation::Write)
        : TIRDeviceValueQuery(registerSet, operation)
        , ProtocolRegisterValues(MapFromSet<_mutable<T>>(registerSet))
    {
        assert(!ProtocolRegistersValues.empty());
    }
};

using TIRDevice64BitQuery = TIRDeviceValueQueryImpl<uint64_t>;
using TIRDeviceSingleBitQuery = TIRDeviceValueQueryImpl<bool>;
