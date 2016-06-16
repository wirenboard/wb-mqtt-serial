
/* vim: set ts=4 sw=4: */

#include "pulsar_device.h"

REGISTER_PROTOCOL("pulsar", TPulsarDevice, TRegisterTypes({
    { TPulsarDevice::REG_DEFAULT, "default", "value", Double, true },
    { TPulsarDevice::REG_SYSTIME, "systime", "value", U64, true }
}));

TPulsarDevice::TPulsarDevice(PDeviceConfig device_config, PAbstractSerialPort port)
    : TSerialDevice(device_config, port) {}

uint16_t TPulsarDevice::CalculateCRC16(const uint8_t *buffer, size_t size)
{
    uint16_t w;
    uint16_t shift_cnt, f;
    const uint8_t *ptrByte;

    uint16_t byte_cnt = size;
    ptrByte = buffer;
    w = (uint16_t) 0xFFFF;

    for (; byte_cnt > 0; byte_cnt--) {
        w = (uint16_t) (w ^ (uint16_t) (*ptrByte++));
        for (shift_cnt = 0; shift_cnt < 8; shift_cnt++) {
            f = (uint8_t) ((w) & 0x01);
            w >>= 1;
            if (f)
                w = (uint16_t) (w ^ 0xa001);
        }
    }

    return w;
}

void TPulsarDevice::WriteBCD(uint64_t value, uint8_t *buffer, size_t size, bool big_endian)
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

void TPulsarDevice::WriteHex(uint64_t value, uint8_t *buffer, size_t size, bool big_endian)
{
    for (size_t i = 0; i < size; i++) {
        buffer[big_endian ? size - i - 1 : i] = value & 0xFF;
        value >>= 8;
    }
}

void TPulsarDevice::WriteDataRequest(uint32_t addr, uint32_t mask, uint16_t id)
{
    Port()->CheckPortOpen();

    uint8_t buf[14];

    /* header = device address in big-endian BCD */
    WriteBCD(addr, buf, sizeof (uint32_t), true);

    /* data request => F == 1, L == 14 */
    buf[4] = 1;
    buf[5] = 14;

    /* data mask in little-endian */
    WriteHex(mask, &buf[6], sizeof (uint32_t), false);

    /* request ID */
    WriteHex(id, &buf[10], sizeof (uint16_t), false);

    /* CRC16 */
    uint16_t crc = CalculateCRC16(buf, 12);
    WriteHex(crc, &buf[12], sizeof (uint16_t), false);

    Port()->WriteBytes(buf, 14);
}

void TPulsarDevice::WriteSysTimeRequest(uint32_t addr, uint16_t id)
{
    Port()->CheckPortOpen();

    uint8_t buf[10];

    /* header = device address in big-endian BCD */
    WriteBCD(addr, buf, sizeof (uint32_t), true);

    /* sys time request => F == 4, L == 10 */
    buf[4] = 1;
    buf[5] = 10;

    /* request ID */
    WriteHex(id, &buf[6], sizeof (uint16_t), false);

    /* CRC16 */
    uint16_t crc = CalculateCRC16(buf, 8);
    WriteHex(crc, &buf[8], sizeof (uint16_t), false);

    Port()->WriteBytes(buf, 10);
}

uint64_t TPulsarDevice::ReadRegister(PRegister reg)
{
    return 0; // WIP
}

void TPulsarDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("Pulsar protocol: writing to registers is not supported");
}
