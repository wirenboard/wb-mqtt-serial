#ifndef WB_MQTT_SERIAL_BCD_UTILS_H
#define WB_MQTT_SERIAL_BCD_UTILS_H

#include <cstdint>
#include <vector>

enum class WordSizes
{
    W8_SZ = 1, W16_SZ = 2, W24_SZ = 3, W32_SZ = 4,
};

// Alignment independent cast of up to four bytes to uint32_t.
// Resulting uint32_t is (potentially) zero padded image of the original BCD byte array.
// For example, BCD32 {0x00, 0x12, 0x34, 0x56} converted to little endian integer
// 0x00123456 (bytes {0x56, 0x34, 0x12, 0x00} or big endian integer 0x00123456
// (bytes {0x00, 0x12, 0x34, 0x56}).
uint32_t PackBytes(uint8_t* bytes, WordSizes size);
// Accepts uint64_t that is a (potentially) zero padded byte image of original BCD byte array
// converted to uint32_t by previous function.
uint64_t PackedBCD2Int(uint64_t packed, WordSizes size);

std::vector<uint8_t> IntToBCDArray(uint64_t value, WordSizes size);

uint32_t IntToPackedBCD(uint32_t value, WordSizes size);
#endif //WB_MQTT_SERIAL_BCD_UTILS_H
