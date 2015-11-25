#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <string>

#include "ivtm_protocol.h"

REGISTER_PROTOCOL("ivtm", TIVTMProtocol);

TIVTMProtocol::TIVTMProtocol(PAbstractSerialPort port)
    : TSerialProtocol(port) {}

void TIVTMProtocol::WriteCommand(uint16_t addr, uint16_t data_addr, uint8_t data_len)
{
    Port()->CheckPortOpen();
    uint8_t buf[16];
    buf[0] = '$';
    
    snprintf((char*) &buf[1], 3, "%02X", addr >> 8);
    snprintf((char*) &buf[3], 3, "%02X", addr & 0xFF );

    buf[5] = 'R';
    buf[6] = 'R';

    snprintf((char*) &buf[7], 3, "%02X", data_addr >> 8);
    snprintf((char*) &buf[9], 3, "%02X", data_addr & 0xFF );

    snprintf((char*) &buf[11], 3, "%02X", data_len);

    uint8_t crc8 = 0;
    for (size_t i=0; i < 13; ++i) { 
        crc8 += buf[i];
    }

    snprintf((char*) &buf[13], 3, "%02X", crc8);
    buf[15] = 0x0d;


    Port()->WriteBytes(buf, 16);
}

static const int MAX_LEN = 100;

bool TIVTMProtocol::DecodeASCIIBytes(uint8_t * buf, uint8_t* result, uint8_t len_bytes)
{
    for (size_t i = 0; i < len_bytes; ++i) {
        result[i] = DecodeASCIIByte(buf + i*2);
    }
    return true;
}


uint8_t TIVTMProtocol::DecodeASCIIByte(uint8_t * buf)
{
    std::string s(buf, buf+2);
    return stoul(s, nullptr, 16);
}

uint16_t TIVTMProtocol::DecodeASCIIWord(uint8_t * buf)
{
    uint8_t decoded_buf[2];
    DecodeASCIIBytes(buf, decoded_buf, 2);

    return (decoded_buf[0] << 8) | decoded_buf[1];
}

void TIVTMProtocol::ReadResponse(uint16_t addr, uint8_t* payload, uint16_t len)
{
    uint8_t buf[MAX_LEN];

    int nread = Port()->ReadFrame(buf, MAX_LEN, FrameTimeoutMs);
    if (nread < 10)
        throw TSerialProtocolTransientErrorException("frame too short");

    if ( (buf[0] != '!') || (buf[5] != 'R') || (buf[6] != 'R')) {
        throw TSerialProtocolTransientErrorException("invalid response header");
    }

    if (buf[nread-1] != 0x0D) {
        throw TSerialProtocolTransientErrorException("invalid response footer");   
    }

    if (DecodeASCIIWord(&buf[1]) != addr) {
        throw TSerialProtocolTransientErrorException("invalid slave addr in response");
    }

    uint8_t crc8 = 0;
    for (size_t i=0; i < (size_t) nread - 3; ++i) { 
        crc8 += buf[i];
    }

    uint8_t actualCrc = DecodeASCIIByte(&buf[nread-3]);

    if (crc8 != actualCrc)
        throw TSerialProtocolTransientErrorException("invalid crc");

    int actualPayloadSize = nread - 10; // in ASCII symbols
    if (len >= 0 && (len * 2) != actualPayloadSize) {
        throw TSerialProtocolTransientErrorException("unexpected frame size");
    } else {
        len = actualPayloadSize / 2;
    }

    DecodeASCIIBytes(buf+7, payload, len);
}


uint64_t TIVTMProtocol::ReadRegister(uint32_t slave, uint32_t address, RegisterFormat fmt, size_t width)
{
    Port()->SkipNoise();

    WriteCommand(slave, address, width);
    uint8_t response[4];
    ReadResponse(slave, response, width);

    uint8_t * p = response;//&response[(address % 2) * 4];

    // the response is little-endian. We inverse the byte order here to make it big-endian.

    return (p[3] << 24) | 
           (p[2] << 16) | 
           (p[1] << 8) |
           p[0];
}


void TIVTMProtocol::WriteRegister(uint32_t mod, uint32_t address, uint64_t value, RegisterFormat)
{
    throw TSerialProtocolException("IVTM protocol: writing register is not supported");
}

void TIVTMProtocol::SetBrightness(uint32_t mod, uint32_t address, uint8_t value)
{
    throw TSerialProtocolException("IVTM protocol: setting brightness not supported");
}



#if 0
int main(int, char**)
{
    try {
        TIVTMProtocol bus("/dev/ttyNSC1");
        bus.Open();
        int v = bus.ReadRegister(0x01, 0x0a);
        std::cout << "value of mod 0x01 reg 0x0a: " << v << std::endl;
        for (int i = 0; i < 8; ++i) {
            bus.WriteRegister(0x01, 0x02 + i, 0x00); // manual control of the channel (low threshold = 0)
            int address = 0x1a + i;
            std::cout << "value of relay " << i << ": " << (int)bus.ReadRegister(0x01, address) << std::endl;
            bus.WriteRegister(0x01, address, 0xff);
            std::cout << "value of relay " << i << " (on): " << (int)bus.ReadRegister(0x01, address) << std::endl;
            sleep(1);
            bus.WriteRegister(0x01, address, 0x00);
            std::cout << "value of relay " << i << " (off): " << (int)bus.ReadRegister(0x01, address) << std::endl;
        }
    } catch (const TIVTMProtocolException& e) {
        std::cerr << "uniel bus error: " << e.what() << std::endl;
    }
    return 0;
}
#endif
