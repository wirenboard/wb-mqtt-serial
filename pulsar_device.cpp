/* vim: set ts=4 sw=4: */

#include "pulsar_device.h"
#include "protocol_register.h"

#include <cstring>
#include <cstdlib>


/* FIXME: move this to configuration file! */
namespace {
    const int FrameTimeout = 300;

    const uint8_t ERROR_FUNCTION_CODE = 0x00;
    const uint8_t ERROR_RESPONSE_SIZE = 11;

    enum PulsarError: uint8_t
    {
        ERR_UNKNOWN                 = 0x00,
        ERR_MISSING_ERROR_CODE      = 0x01,
        ERR_REQUEST_BITMASK         = 0x02,
        ERR_REQUEST_SIZE            = 0x03,
        ERR_MISSING_PARAMETER       = 0x04,
        ERR_AUTHORIZATION_REQUIRED  = 0x05,
        ERR_OUT_OF_RANGE            = 0x06,
        ERR_MISSING_ARCHIVE_TYPE    = 0x07,
        ERR_TOO_MANY_ARCHIVE_VALUES = 0x08
    };

    void ThrowPulsarException(uint8_t code)
    {
        const char * message = nullptr;
        bool is_transient = true;

        switch (code) {
            case ERR_UNKNOWN:
                message = "unknown error";
                break;
            case ERR_MISSING_ERROR_CODE:
                message = "missing error code";
                break;
            case ERR_REQUEST_BITMASK:
                message = "request bitmask error";
                is_transient = false;
                break;
            case ERR_REQUEST_SIZE:
                message = "request size error";
                break;
            case ERR_MISSING_PARAMETER:
                message = "missing parameter";
                is_transient = false;
                break;
            case ERR_AUTHORIZATION_REQUIRED:
                message = "write locked, authorization required";
                break;
            case ERR_OUT_OF_RANGE:
                message = "value to write (parameter) is out of range";
                break;
            case ERR_MISSING_ARCHIVE_TYPE:
                message = "missing requested archive type";
                break;
            case ERR_TOO_MANY_ARCHIVE_VALUES:
                message = "too many archive values in one package";
                break;
            default:
                throw TSerialDeviceTransientErrorException("invalid pulsar error code (" + std::to_string(code) + ")");
        }

        if (is_transient) {
            throw TSerialDeviceTransientErrorException(message);
        } else {
            throw TSerialDevicePermanentErrorException(message);
        }
    }
}

REGISTER_BASIC_INT_PROTOCOL("pulsar", TPulsarDevice, TRegisterTypes({
    { TPulsarDevice::REG_DEFAULT, "default", "value", TMemoryBlockType::Variadic, Double, true },
    { TPulsarDevice::REG_SYSTIME, "systime", "value", 8, U64, true }
}));

TPulsarDevice::TPulsarDevice(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TPulsarDevice>>(device_config, port, protocol)
    , RequestID(0)
{}

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

    bool is_error = response[4] == ERROR_FUNCTION_CODE;

    uint8_t response_size = is_error ? ERROR_RESPONSE_SIZE : exp_size;

    if (nread != response_size)
        throw TSerialDeviceTransientErrorException("unexpected end of frame");

    if (response_size != response[5])
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

    if (is_error) {
        uint8_t error_code;
        memcpy(&error_code, &response[nread - 5], 1);

        ThrowPulsarException(error_code);
    }

    /* copy payload data to external buffer */
    memcpy(payload, response + 6, size);
}

uint64_t TPulsarDevice::ReadDataRegister(const PProtocolRegister & reg)
{
    // raw payload data
    uint8_t payload[sizeof (uint64_t)];

    // form register mask from address
    uint32_t mask = 1 << reg->Address; // TODO: register range or something like this

    auto byteCount = reg->GetUsedByteCount();

    // send data request and receive response
    WriteDataRequest(SlaveId, mask, RequestID);
    ReadResponse(SlaveId, payload, byteCount, RequestID);

    ++RequestID;

    // decode little-endian double64_t value
    return ReadHex(payload, byteCount, false);
}

uint64_t TPulsarDevice::ReadSysTimeRegister(const PProtocolRegister & reg)
{
    // raw payload data
    uint8_t payload[6];

    // send system time request and receive response
    WriteSysTimeRequest(SlaveId, RequestID);
    ReadResponse(SlaveId, payload, sizeof (payload), RequestID);

    ++RequestID;

    // decode little-endian double64_t value
    return ReadHex(payload, sizeof (payload), false);
}

uint64_t TPulsarDevice::ReadProtocolRegister(const PProtocolRegister & reg)
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

void TPulsarDevice::WriteProtocolRegister(const PProtocolRegister & reg, uint64_t value)
{
    throw TSerialDeviceException("Pulsar protocol: writing to registers is not supported");
}
