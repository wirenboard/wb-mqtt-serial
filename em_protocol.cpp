#include "em_protocol.h"
#include "crc16.h"

TEMProtocol::TEMProtocol(PAbstractSerialPort port)
    : TSerialProtocol(port) {}

void TEMProtocol::EnsureSlaveConnected(uint8_t slave)
{
    if (slaveMap[slave])
        return;

    for (int n = N_CONN_ATTEMPTS; n > 0; n--) {
        Port()->SkipNoise();
        if (ConnectionSetup(slave)) {
            slaveMap[slave] = true;
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

void TEMProtocol::ReadResponse(uint8_t slave, int expectedByte1, uint8_t* payload, int len)
{
    if (len + 4 > MAX_LEN)
        throw TSerialProtocolException("expected response too long");

    uint8_t buf[MAX_LEN], *p = buf;
    if ((*p++ = Port()->ReadByte()) != slave) {
        Port()->SkipNoise();
        throw TSerialProtocolTransientErrorException("invalid slave id");
    }
    if (expectedByte1 >= 0 && (*p++ = Port()->ReadByte()) != expectedByte1) {
        Port()->SkipNoise();
        throw TSerialProtocolTransientErrorException("invalid command code in the response");
    }
    while (len--)
        *p++ = *payload++ = Port()->ReadByte();
    uint16_t crc = CRC16::CalculateCRC16(buf, p - buf);
    uint8_t crc1 = Port()->ReadByte(), crc2 = Port()->ReadByte();
    uint16_t actualCrc = (crc1 << 8) + crc2;
    if (crc != actualCrc)
        throw TSerialProtocolTransientErrorException("invalid crc");
}

void TEMProtocol::WriteRegister(uint32_t, uint32_t, uint64_t, RegisterFormat) {
    throw TSerialProtocolException("EM protocol: writing to registers not supported");
}

// XXX FIXME: leaky abstraction (need to refactor)
// Perhaps add 'brightness' register format
void TEMProtocol::SetBrightness(uint32_t, uint32_t, uint8_t) {
    throw TSerialProtocolException("EM protocol: setting brightness not supported");
}
