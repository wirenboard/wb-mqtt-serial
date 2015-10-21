#include "milur_protocol.h"
#include "crc16.h"

namespace {
    void GetRegType(RegisterFormat fmt, int* size, bool* bcd) {
        *bcd = false;
        switch (fmt) {
        case BCD8:
            *bcd = true;
        case U8:
            *size = 1;
            break;

        case BCD16:
            *bcd = true;
        case U16:
            *size = 2;
            break;

        case BCD24:
            *bcd = true;
        case U24:
            *size = 3;
            break;

        case BCD32:
            *bcd = true;
        case U32:
            *size = 4;
            break;

        default:
            throw TSerialProtocolException("milur: unsupported register format");
        }
    }
}

TMilurProtocol::TMilurProtocol(const TSerialPortSettings& settings, bool debug)
    : TSerialProtocol(settings, debug) {}

void TMilurProtocol::EnsureSlaveConnected(uint8_t slave)
{
    if (slaveMap[slave])
        return;
    SkipNoise();
    static uint8_t setupCmd[] = {
        // full: 0xff, 0x08, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xed
        ACCESS_LEVEL, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    uint8_t buf[MAX_LEN];
    for (int n = N_CONN_ATTEMPTS; n > 0; n--) {
        WriteCommand(slave, 0x08, setupCmd, 7);
        try {
            ReadResponse(slave, 0x08, buf, 1);
            if (buf[0] != ACCESS_LEVEL)
                throw TSerialProtocolException("invalid milur access level in response");
            slaveMap[slave] = true;
            return;
        } catch (TSerialProtocolTransientErrorException&) {
            // retry upon response from a wrong slave
            continue;
        }
    }

    throw TSerialProtocolException("failed to establish Milur connection");
}

void TMilurProtocol::WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len)
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
    WriteBytes(buf, p - buf);
}

void TMilurProtocol::ReadResponse(uint8_t slave, uint8_t cmd, uint8_t* payload, int len)
{
    if (len + 4 > MAX_LEN)
        throw TSerialProtocolException("expected response too long");

    uint8_t buf[MAX_LEN], *p = buf;
    if ((*p++ = ReadByte()) != slave) {
        SkipNoise();
        throw TSerialProtocolTransientErrorException("invalid slave id");
    }
    if ((*p++ = ReadByte()) != cmd) {
        SkipNoise();
        throw TSerialProtocolTransientErrorException("invalid command code in the response");
    }
    while (len--)
        *p++ = *payload++ = ReadByte();
    uint16_t crc = CRC16::CalculateCRC16(buf, p - buf);
    uint8_t crc1 = ReadByte(), crc2 = ReadByte();
    uint16_t actualCrc = (crc1 << 8) + crc2;
    if (crc != actualCrc)
        throw TSerialProtocolTransientErrorException("invalid crc");
}

#include <iostream>
uint64_t TMilurProtocol::ReadRegister(uint8_t slave, uint8_t address, RegisterFormat fmt)
{
    int size;
    bool bcd;
    GetRegType(fmt, &size, &bcd);

    EnsureSlaveConnected(slave);
    WriteCommand(slave, 0x01, &address, 1);
    uint8_t buf[MAX_LEN], *p = buf;
    ReadResponse(slave, 0x01, buf, size + 2);
    if (*p++ != address)
        throw TSerialProtocolTransientErrorException("bad register address in the response");
    if (*p++ != size)
        throw TSerialProtocolTransientErrorException("bad register size in the response");

    uint64_t r = 0;
    if (bcd) {
        for (int i = 0, mul = 1; i < size; ++i, mul *= 100) {
            int v = buf[i + 2];
            r += ((v >> 4) * 10 + (v & 0x0f)) * mul;
        }
    } else {
        for (int i = 0; i < size; ++i) {
            r += buf[i + 2] << (i * 8);
        }
    }

    return r;
}

void TMilurProtocol::WriteRegister(uint8_t, uint8_t, uint64_t, RegisterFormat) {
    throw TSerialProtocolException("milur: writing to registers not supported");
}

// XXX FIXME: leaky abstraction (need to refactor)
// Perhaps add 'brightness' register format
void TMilurProtocol::SetBrightness(uint8_t, uint8_t, uint8_t) {
    throw TSerialProtocolException("milur: setting brightness not supported");
}

#if 0
#include <iomanip>
#include <iostream>
int main(int, char**)
{
    try {
        TSerialPortSettings settings("/dev/ttyNSC0", 9600, 'N', 8, 2, 1000);
        TMilurProtocol milur(settings, true);
        milur.Open();
        std::ios::fmtflags f(std::cerr.flags());
        int v = milur.ReadRegister(0xff, 102, U24);
        std::cerr << "value of mod 0xff reg 0x66: 0x" << std::setw(8) << std::hex << v << std::endl;
        std::cerr.flags(f);
        std::cerr << "dec value: " << v << std::endl;

        int v1 = milur.ReadRegister(0xff, 118, BCD32);
        std::cerr << "value of mod 0xff reg 0x76: " << v1 << std::endl;
    } catch (const TSerialProtocolException& e) {
        std::cerr << "milur: " << e.what() << std::endl;
    }
    return 0;
}
#endif

// TBD: custom password?
// TBD: reconnection (session setup)
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
