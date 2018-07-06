#pragma once

#include <limits>
#include <stdint.h>

using TBitIndex = uint16_t;
using TByteIndex = uint16_t;
using TValueSize = uint16_t;
using TValue = uint64_t;

constexpr uint64_t MAX_MEMORY_BLOCK_WIDTH       = std::numeric_limits<TBitIndex>::max();
constexpr uint64_t MAX_MEMORY_BLOCK_BIT_INDEX   = MAX_MEMORY_BLOCK_WIDTH - 1;
constexpr uint64_t MAX_MEMORY_BLOCK_SIZE        = MAX_MEMORY_BLOCK_WIDTH / 8;
constexpr uint64_t MAX_MEMORY_BLOCK_BYTE_INDEX  = MAX_MEMORY_BLOCK_SIZE - 1;
constexpr uint64_t MAX_MEMORY_BLOCK_VALUE_INDEX = MAX_MEMORY_BLOCK_BYTE_INDEX; // edge case: all values in block are 1 byte
constexpr uint64_t MAX_NUMERIC_VALUE_SIZE       = sizeof(TValue);
constexpr uint64_t MAX_NUMERIC_VALUE_WIDTH      = MAX_NUMERIC_VALUE_SIZE * 8;
constexpr uint64_t MAX_STRING_VALUE_WIDTH       = std::numeric_limits<TValueSize>::max();
constexpr uint64_t MAX_STRING_VALUE_SIZE        = MAX_STRING_VALUE_WIDTH / 8;
