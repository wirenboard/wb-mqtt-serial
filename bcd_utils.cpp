#include <cassert>
#include "bcd_utils.h"

// Placing BCD bytes to integer in a way it reads
// the same as original byte sequence:
// bytes: {0x12, 0x34, 0x56, 0x78}
// integer value: 0x12345678
// actual bytes in integer: 0x78563412
uint32_t PackBCD(uint8_t* bytes, BCDSizes size)
{
    uint32_t ret = 0U;
    switch (size) {
    case BCDSizes::BCD32_SZ:
        ret |= (*bytes++ << 24);
    case BCDSizes::BCD24_SZ:
        ret |= (*bytes++ << 16);
    case BCDSizes::BCD16_SZ:
        ret |= (*bytes++ << 8);
    case BCDSizes::BCD8_SZ:
        ret |= (*bytes << 0);
    }
    return ret;
}

uint64_t PackedBCD2Int(uint64_t packed, BCDSizes size)
{
    uint32_t result = 0;
    int exp = 1;
    for (int i = 0; i < size; ++i) {
        auto tmp = static_cast<uint8_t>(packed >> (i * 8));
        result += ((tmp & 0x0f) * exp);
        exp *= 10;
        result += ((tmp >> 4) * exp);
        exp *= 10;
    }
    return result;
}
