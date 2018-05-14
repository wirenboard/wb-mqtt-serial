#pragma once

#include "ir_device_query.h"
#include "memory_block.h"

#include <cstring>

#include <iostream>

//template <typename T>
struct TIRDeviceValueQueryImpl final: TIRDeviceValueQuery
{


    //static_assert(std::is_fundamental<T>::value, "query can store only values of fundamental types");





protected:
    explicit TIRDeviceValueQueryImpl(const TPSet<PMemoryBlock> & memoryBlockSet, EQueryOperation operation = EQueryOperation::Write)
        : TIRDeviceValueQuery(memoryBlockSet, operation)
        ,
};

// using TIRDevice64BitQuery = TIRDeviceValueQueryImpl<uint64_t>;
// using TIRDeviceSingleBitQuery = TIRDeviceValueQueryImpl<bool>;
