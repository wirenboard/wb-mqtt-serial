/* vim: set ts=4 sw=4: */

#include <cstdlib>
#include <cstring>

#include "pulsar_device.h"

namespace
{
    const TRegisterTypes RegisterTypes{{TPulsarDevice::REG_DEFAULT, "default", "value", Double, true},
                                       {TPulsarDevice::REG_SYSTIME, "systime", "value", U64, true}};
}

void TPulsarDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("pulsar", RegisterTypes),
        new TBasicDeviceFactory<TPulsarDevice>("#/definitions/simple_device", "#/definitions/common_channel"));
}

TPulsarDevice::TPulsarDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      RequestID(0)
{}

uint16_t TPulsarDevice::CalculateCRC16(const uint8_t* buffer, size_t size)
{
    uint16_t w;
    uint16_t shift_cnt, f;
    const uint8_t* ptrByte;

    uint16_t byte_cnt = size;
    ptrByte = buffer;
    w = (uint16_t)0xFFFF;

    for (; byte_cnt > 0; byte_cnt--) {
        w = (uint16_t)(w ^ (uint16_t)(*ptrByte++));
        for (shift_cnt = 0; shift_cnt < 8; shift_cnt++) {
            f = (uint8_t)((w)&0x01);
            w >>= 1;
            if (f)
                w = (uint16_t)(w ^ 0xa001);
        }
    }

    return w;
}

void TPulsarDevice::WriteBCD(uint64_t value, uint8_t* buffer, size_t size, bool big_endian)
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

void TPulsarDevice::WriteHex(uint64_t value, uint8_t* buffer, size_t size, bool big_endian)
{
    for (size_t i = 0; i < size; i++) {
        buffer[big_endian ? size - i - 1 : i] = value & 0xFF;
        value >>= 8;
    }
}

uint64_t TPulsarDevice::ReadBCD(const uint8_t* buffer, size_t size, bool big_endian)
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

uint64_t TPulsarDevice::ReadHex(const uint8_t* buffer, size_t size, bool big_endian)
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
    WriteBCD(addr, buf, sizeof(uint32_t), true);

    /* data request => F == 1, L == 14 */
    buf[4] = 1;
    buf[5] = 14;

    /* data mask in little-endian */
    WriteHex(mask, &buf[6], sizeof(uint32_t), false);

    /* request ID */
    WriteHex(id, &buf[10], sizeof(uint16_t), false);

    /* CRC16 */
    uint16_t crc = CalculateCRC16(buf, 12);
    WriteHex(crc, &buf[12], sizeof(uint16_t), false);

    Port()->WriteBytes(buf, 14);
}

void TPulsarDevice::WriteSysTimeRequest(uint32_t addr, uint16_t id)
{
    Port()->CheckPortOpen();

    uint8_t buf[10];

    /* header = device address in big-endian BCD */
    WriteBCD(addr, buf, sizeof(uint32_t), true);

    /* sys time request => F == 4, L == 10 */
    buf[4] = 1;
    buf[5] = 10;

    /* request ID */
    WriteHex(id, &buf[6], sizeof(uint16_t), false);

    /* CRC16 */
    uint16_t crc = CalculateCRC16(buf, 8);
    WriteHex(crc, &buf[8], sizeof(uint16_t), false);

    Port()->WriteBytes(buf, 10);
}

void TPulsarDevice::ReadResponse(uint32_t addr, uint8_t* payload, size_t size, uint16_t id)
{
    const int exp_size = size + 10; /* payload size + service bytes */
    std::vector<uint8_t> response(exp_size);

    int nread = Port()
                    ->ReadFrame(response.data(),
                                response.size(),
                                DeviceConfig()->ResponseTimeout,
                                DeviceConfig()->FrameTimeout,
                                [](uint8_t* buf, int size) { return size >= 6 && size == buf[5]; })
                    .Count;

    /* check size */
    if (nread < 6)
        throw TSerialDeviceTransientErrorException("frame is too short");

    if (nread != exp_size)
        throw TSerialDeviceTransientErrorException("unexpected end of frame");

    if (exp_size != response[5])
        throw TSerialDeviceTransientErrorException("unexpected frame length");

    /* check CRC16 */
    uint16_t crc_recv = ReadHex(&response[nread - 2], sizeof(uint16_t), false);
    if (crc_recv != CalculateCRC16(response.data(), nread - 2))
        throw TSerialDeviceTransientErrorException("CRC mismatch");

    /* check address */
    uint32_t addr_recv = ReadBCD(response.data(), sizeof(uint32_t), true);
    if (addr_recv != addr)
        throw TSerialDeviceTransientErrorException("slave address mismatch");

    /* check request ID */
    uint16_t id_recv = ReadHex(&response[nread - 4], sizeof(uint16_t), false);
    if (id_recv != id)
        throw TSerialDeviceTransientErrorException("request ID mismatch");

    /* copy payload data to external buffer */
    memcpy(payload, response.data() + 6, size);
}

TRegisterValue TPulsarDevice::ReadDataRegister(const TRegisterConfig& reg)
{
    auto addr = GetUint32RegisterAddress(reg.GetAddress());
    // raw payload data
    uint8_t payload[sizeof(uint64_t)];

    // form register mask from address
    uint32_t mask = 1 << addr;

    // send data request and receive response
    WriteDataRequest(SlaveId, mask, RequestID);
    ReadResponse(SlaveId, payload, reg.GetByteWidth(), RequestID);

    ++RequestID;

    // decode little-endian double64_t value
    return TRegisterValue{ReadHex(payload, reg.GetByteWidth(), false)};
}

TRegisterValue TPulsarDevice::ReadSysTimeRegister(const TRegisterConfig& reg)
{
    // raw payload data
    uint8_t payload[6];

    // send system time request and receive response
    WriteSysTimeRequest(SlaveId, RequestID);
    ReadResponse(SlaveId, payload, sizeof(payload), RequestID);

    ++RequestID;

    // decode little-endian double64_t value
    return TRegisterValue{ReadHex(payload, sizeof(payload), false)};
}

TRegisterValue TPulsarDevice::ReadRegisterImpl(const TRegisterConfig& reg)
{
    Port()->SkipNoise();

    switch (reg.Type) {
        case REG_DEFAULT:
            return ReadDataRegister(reg);
        case REG_SYSTIME:
            return ReadSysTimeRegister(reg);
        default:
            throw TSerialDeviceException("Pulsar protocol: wrong register type");
    }
}
