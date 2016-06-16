/* vim: set ts=4 sw=4: */

#include <cstring>
#include <cstdlib>

#include "pulsar_device.h"

/* FIXME: move this to configuration file! */
namespace {
    const int FrameTimeout = 300;
}

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

uint64_t TPulsarDevice::ReadBCD(const uint8_t *buffer, size_t size, bool big_endian)
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

uint64_t TPulsarDevice::ReadHex(const uint8_t *buffer, size_t size, bool big_endian)
{
    uint64_t result = 0;

    for (size_t i = 0; i < size; i++) {
        result <<= 8;
        result |= buffer[big_endian ? i : size - i - 1];
    }

    return result;
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

void TPulsarDevice::ReadResponse(uint32_t addr, uint8_t *payload, size_t size, uint16_t id)
{
    const int exp_size = size + 10; /* payload size + service bytes */
    uint8_t response[exp_size];

    int nread = Port()->ReadFrame(response, exp_size, std::chrono::milliseconds(FrameTimeout),
            [] (uint8_t *buf, int size) {
                    return size >= 6 && size == buf[5];
            });

    /* check size */
    if (nread < 6)
        throw TSerialDeviceTransientErrorException("frame is too short");

    if (nread != exp_size)
        throw TSerialDeviceTransientErrorException("unexpected end of frame");

    if (exp_size != response[5])
        throw TSerialDeviceTransientErrorException("unexpected frame length");

    /* check CRC16 */
    uint16_t crc_recv = ReadHex(&response[nread - 2], sizeof (uint16_t), false);
    if (crc_recv != CalculateCRC16(response, nread - 2))
        throw TSerialDeviceTransientErrorException("CRC mismatch");

    /* check address */
    uint32_t addr_recv = ReadBCD(response, sizeof (uint32_t), true);
    if (addr_recv != addr)
        throw TSerialDeviceTransientErrorException("slave address mismatch");

    /* check request ID */
    uint16_t id_recv = ReadHex(&response[nread - 4], sizeof (uint16_t), false);
    if (id_recv != id)
        throw TSerialDeviceTransientErrorException("request ID mismatch");

    /* copy payload data to external buffer */
    memcpy(payload, response + 6, size);
}

uint64_t TPulsarDevice::ReadDataRegister(PRegister reg)
{
    // raw payload data
    uint8_t payload[sizeof (uint64_t)];

    // form register mask from address
    uint32_t mask = 1 << reg->Address; // TODO: register range or something like this

    // form request ID as random value (strength doesn't matter so don't care about srand()
    uint16_t id = rand() & 0xFFFF;

    // send data request and receive response
    WriteDataRequest(reg->Slave->Id, mask, id);
    ReadResponse(reg->Slave->Id, payload, sizeof (payload), id);

    // decode little-endian double64_t value
    return ReadHex(payload, sizeof (uint64_t), false);
}

uint64_t TPulsarDevice::ReadSysTimeRegister(PRegister reg)
{
    // raw payload data
    uint8_t payload[6];

    // form request ID as random value (strength doesn't matter so don't care about srand()
    uint16_t id = rand() & 0xFFFF;

    // send system time request and receive response
    WriteSysTimeRequest(reg->Slave->Id, id);
    ReadResponse(reg->Slave->Id, payload, sizeof (payload), id);

    // decode little-endian double64_t value
    return ReadHex(payload, sizeof (payload), false);
}

uint64_t TPulsarDevice::ReadRegister(PRegister reg)
{
    Port()->SkipNoise();

    switch (reg->Type) {
    case REG_DEFAULT:
        return ReadDataRegister(reg);
    case REG_SYSTIME: // TODO: think about return value
        return ReadSysTimeRegister(reg);   
    default:
        throw TSerialDeviceException("Pulsar protocol: wrong register type");
    }
}

void TPulsarDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("Pulsar protocol: writing to registers is not supported");
}
