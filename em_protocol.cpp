#include <cstring>

#include "em_protocol.h"
#include "crc16.h"

TEMProtocol::TEMProtocol(PDeviceConfig device_config, PAbstractSerialPort port)
    : TSerialProtocol(port), password(device_config->Password),
      accessLevel(device_config->AccessLevel) {}

void TEMProtocol::EnsureSlaveConnected(uint8_t slave, bool force)
{
    if (!force && connectedSlaves.find(slave) != connectedSlaves.end())
        return;

    connectedSlaves.erase(slave);
    for (int n = N_CONN_ATTEMPTS; n > 0; n--) {
        Port()->SkipNoise();
        if (ConnectionSetup(slave)) {
            connectedSlaves.insert(slave);
            return;
        }
    }

    throw TSerialProtocolException("failed to establish meter connection");
}

void TEMProtocol::WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len)
{
    uint8_t buf[MAX_LEN], *p = buf;
    if (len + 4 > MAX_LEN)
        throw TSerialProtocolException("outgoing command too long");
    *p++ = slave;
    *p++ = cmd;
    while (len--)
        *p++ = *payload++;
    uint16_t crc = CRC16::CalculateCRC16(buf, p - buf);
    *p++ = crc >> 8;
    *p++ = crc & 0xff;
    Port()->WriteBytes(buf, p - buf);
}

bool TEMProtocol::ReadResponse(uint8_t slave, int expectedByte1, uint8_t* payload, int len)
{
    uint8_t buf[MAX_LEN], *p = buf;
    int nread = Port()->ReadFrame(buf, MAX_LEN);
    if (nread < 4)
        throw TSerialProtocolTransientErrorException("frame too short");

    uint16_t crc = CRC16::CalculateCRC16(buf, nread - 2),
        crc1 = buf[nread - 2],
        crc2 = buf[nread - 1],
        actualCrc = (crc1 << 8) + crc2;
    if (crc != actualCrc)
        throw TSerialProtocolTransientErrorException("invalid crc");

    if (*p++ != slave)
        throw TSerialProtocolTransientErrorException("invalid slave id");

    const char* msg;
    ErrorType err = CheckForException(buf, nread, &msg);
    if (err == NO_OPEN_SESSION)
        return false;
    else if (err != NO_ERROR)
        throw TSerialProtocolTransientErrorException(msg);

    if (expectedByte1 >= 0 && *p++ != expectedByte1)
        throw TSerialProtocolTransientErrorException("invalid command code in the response");

    int actualPayloadSize = nread - (p - buf) - 2;
    if (len >= 0 && len != actualPayloadSize)
        throw TSerialProtocolTransientErrorException("unexpected frame size");
    else
        len = actualPayloadSize;

    std::memcpy(payload, p, len);
    return true;
}

void TEMProtocol::Talk(uint8_t slave, uint8_t cmd, uint8_t* payload, int payloadLen,
                       int expectedByte1, uint8_t* respPayload, int respPayloadLen)
{
    EnsureSlaveConnected(slave);
    WriteCommand(slave, cmd, payload, payloadLen);
    try {
        while (!ReadResponse(slave, expectedByte1, respPayload, respPayloadLen)) {
            EnsureSlaveConnected(slave, true);
            WriteCommand(slave, cmd, payload, payloadLen);
        }
    } catch (const TSerialProtocolTransientErrorException& e) {
        Port()->SkipNoise();
        throw;
    }
}

void TEMProtocol::WriteRegister(PRegister, uint64_t)
{
    throw TSerialProtocolException("EM protocol: writing to registers not supported");
}
