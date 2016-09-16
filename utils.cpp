#include "utils.h"

/*!
 * Store given value in memory in BCD format
 * \param value Value to store
 * \param buffer Pointer to memory
 * \param size Buffer size
 * \param big_endian Endianess (default: true -> big endian)
 */
void Utils::WriteBCD(uint64_t value, uint8_t *buffer, size_t size, bool big_endian)
{
    for (size_t i = 0; i < size; i++) {
        // form byte from the end of value
        uint8_t byte = value % 10;
        value /= 10;
        byte |= (value % 10) << 4;
        value /= 10;

        buffer[big_endian ? size - i - 1 : i] = byte;
    }
}

/*!
 * Store given value in memory in basic hex format
 * \param value Value to store
 * \param buffer Pointer to memory
 * \param size Buffer size
 * \param big_endian Endianess (default: true -> big endian)
 */
void Utils::WriteHex(uint64_t value, uint8_t *buffer, size_t size, bool big_endian)
{
    for (size_t i = 0; i < size; i++) {
        buffer[big_endian ? size - i - 1 : i] = value & 0xFF;
        value >>= 8;
    }
}

/*!
 * Read value from BCD formatted buffer
 * \param buffer Pointer to buffer
 * \param size Buffer size
 * \param big_endian Endianess (default: true -> big endian)
 * \return Stored value
 */
uint64_t Utils::ReadBCD(const uint8_t *buffer, size_t size, bool big_endian)
{
    uint64_t result = 0;

    for (size_t i = 0; i < size; i++) {
        result *= 100;

        uint8_t bcd_byte = buffer[big_endian ? i : size - i - 1];
        uint8_t dec_byte = (bcd_byte & 0x0F) + 10 * ((bcd_byte >> 4) & 0x0F);

        result += dec_byte;
    }

    return result;
}

/*!
 * Read value from basic hex formatted buffer
 * \param buffer Pointer to buffer
 * \param size Buffer size
 * \param big_endian Endianess (default: true -> big endian)
 * \return Stored value
 */
uint64_t Utils::ReadHex(const uint8_t *buffer, size_t size, bool big_endian)
{
    uint64_t result = 0;

    for (size_t i = 0; i < size; i++) {
        result <<= 8;
        result |= buffer[big_endian ? i : size - i - 1];
    }

    return result;
}

