#pragma once

#include <cstdlib>
#include <stdint.h>

namespace Utils {

void WriteBCD(uint64_t data, uint8_t *buffer, size_t size, bool big_endian = true);
void WriteHex(uint64_t data, uint8_t *buffer, size_t size, bool big_endian = true);
uint64_t ReadBCD(const uint8_t *data, size_t size, bool big_endian = true);
uint64_t ReadHex(const uint8_t *data, size_t size, bool big_endian = true);

};
