#ifndef WB_MQTT_SERIAL_BCD_UTILS_H
#define WB_MQTT_SERIAL_BCD_UTILS_H

#include <cstdint>

enum BCDSizes
{
    BCD8_SZ = 1, BCD16_SZ = 2, BCD24_SZ = 3, BCD32_SZ = 4,
};

// Alignment independent cast of up to four BCD bytes to uint32_t.
// Resulting uint32_t is (potentially) zero padded image of the original BCD byte array.
// For example, BCD32 {0x00, 0x12, 0x34, 0x56} converted to little endian integer
// 0x00123456 (bytes {0x56, 0x34, 0x12, 0x00} or big endian integer 0x00123456
// (bytes {0x00, 0x12, 0x34, 0x56}).
uint32_t PackBCD(uint8_t* bytes, BCDSizes size);
// Accepts uint64_t that is a (potentially) zero padded byte image of original BCD byte array
// converted to uint32_t by previous function.
uint64_t PackedBCD2Int(uint64_t packed, BCDSizes size);

#endif //WB_MQTT_SERIAL_BCD_UTILS_H
