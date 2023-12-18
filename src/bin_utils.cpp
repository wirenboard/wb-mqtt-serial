#include "bin_utils.h"
#include <assert.h>

uint64_t BinUtils::GetLSBMask(uint8_t bitCount)
{
    if (bitCount < sizeof(uint64_t) * 8) {
        return (uint64_t(1) << bitCount) - 1;
    }
    return 0xFFFFFFFFFFFFFFFF;
}
