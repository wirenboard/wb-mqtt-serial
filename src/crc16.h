#pragma once

#include <stdint.h>

namespace CRC16
{
    uint16_t CalculateCRC16(const uint8_t* buffer, uint16_t len);
}
