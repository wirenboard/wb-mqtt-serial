#include <cassert>
#include "bcd_utils.h"

// Placing 1-4 bytes to integer in a way it reads
// the same as original byte sequence:
// bytes: {0x12, 0x34, 0x56, 0x78}
// integer value: 0x12345678
// actual bytes in integer: 0x78563412
uint32_t PackBytes(uint8_t* bytes, WordSizes size)
{
    uint32_t ret = 0U;
    /* note the absense of breaks here.
    the execution will propagate to the very end of
    the switch statement*/
    switch (size) {
    case WordSizes::W32_SZ:
        ret |= (*bytes++ << 24);
    case WordSizes::W24_SZ:
        ret |= (*bytes++ << 16);
    case WordSizes::W16_SZ:
        ret |= (*bytes++ << 8);
    case WordSizes::W8_SZ:
        ret |= (*bytes << 0);
    }
    return ret;
}

uint64_t PackedBCD2Int(uint64_t packed, WordSizes size)
{
    uint32_t result = 0;
    int exp = 1;
    for (unsigned i = 0; i < static_cast<unsigned>(size); ++i) {
        auto tmp = static_cast<uint8_t>(packed >> (i * 8));
        result += ((tmp & 0x0f) * exp);
        exp *= 10;
        result += ((tmp >> 4) * exp);
        exp *= 10;
    }
    return result;
}
std::vector<uint8_t> IntToBCDArray(uint64_t value, WordSizes size)
{
    std::vector<uint8_t> result;
    result.resize(static_cast<unsigned>(size));

    for (unsigned i = 0; i < static_cast<unsigned>(size); i++) {
        // form byte from the end of value
        uint8_t byte = value % 10;
        value /= 10;
        byte |= (value % 10) << 4;
        value /= 10;
        result[static_cast<unsigned>(size) - i - 1] = byte;
    }
    return result;
}

uint32_t IntToPackedBCD(uint32_t value, WordSizes size)
{
    auto bcdArray = IntToBCDArray(value, size);
    return PackBytes(bcdArray.data(), size);
}
