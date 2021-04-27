#include "bin_utils.h"
#include <assert.h>

uint64_t BinUtils::MersenneNumber(uint8_t bitCount)
{
    assert(bitCount <= sizeof(uint64_t)*8);
    return (uint64_t(1) << bitCount) - 1;
}

