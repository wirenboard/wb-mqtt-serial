#pragma once

#include <limits>
#include <stdint.h>

using TMemoryBlockBitIndex = uint16_t;
using TValue = uint64_t;

constexpr uint64_t MAX_MEMORY_BLOCK_WIDTH       = std::numeric_limits<TMemoryBlockBitIndex>::max();
constexpr uint64_t MAX_MEMORY_BLOCK_BIT_INDEX   = MAX_MEMORY_BLOCK_WIDTH - 1;
constexpr uint64_t MAX_MEMORY_BLOCK_SIZE        = MAX_MEMORY_BLOCK_WIDTH / 8;
constexpr uint64_t MAX_MEMORY_BLOCK_BYTE_INDEX  = MAX_MEMORY_BLOCK_SIZE - 1;
constexpr uint64_t MAX_MEMORY_BLOCK_VALUE_INDEX = MAX_MEMORY_BLOCK_BYTE_INDEX; // edge case: all values in block are 1 byte
constexpr uint64_t MAX_VALUE_SIZE               = sizeof(TValue);
constexpr uint64_t MAX_VALUE_WIDTH              = MAX_VALUE_SIZE * 8;
