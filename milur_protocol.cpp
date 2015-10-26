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
        case S32:
            *size = 4;
            break;

        default:
            throw TSerialProtocolException("milur: unsupported register format");
        }
    }
}

TMilurProtocol::TMilurProtocol(PAbstractSerialPort port)
    : TEMProtocol(port) {}

bool TMilurProtocol::ConnectionSetup(uint8_t slave)
{
    static uint8_t setupCmd[] = {
        // full: 0xff, 0x08, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xed
        ACCESS_LEVEL, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    uint8_t buf[MAX_LEN];
    WriteCommand(slave, 0x08, setupCmd, 7);
    try {
        if (!ReadResponse(slave, 0x08, buf, 1))
            return false;
        if (buf[0] != ACCESS_LEVEL)
            throw TSerialProtocolException("invalid milur access level in response");
        return true;
    } catch (TSerialProtocolTransientErrorException&) {
            // retry upon response from a wrong slave
        return false;
    }
}

TEMProtocol::ErrorType TMilurProtocol::CheckForException(uint8_t* frame, int len, const char** message)
{
    if (len != 6 || !(frame[1] & 0x80)) {
        *message = 0;
        return TEMProtocol::NO_ERROR;
    }

    switch (frame[2]) {
    case 0x01:
        *message = "Illegal function";
        break;
    case 0x02:
        *message = "Illegal data address";
        break;
    case 0x03:
        *message = "Illegal data value";
        break;
    case 0x04:
        *message = "Slave device failure";
        break;
    case 0x05:
        *message = "Acknowledge";
        break;
    case 0x06:
        *message = "Slave device busy";
        break;
    case 0x07:
        *message = "EEPROM access error";
        break;
    case 0x08:
        *message = "Session closed";
        return TEMProtocol::NO_OPEN_SESSION;
    case 0x09:
        *message = "Access denied";
        break;
    case 0x0a:
        *message = "CRC error";
        break;
    case 0x0b:
        *message = "Frame incorrect";
        break;
    case 0x0c:
        *message = "Jumper absent";
        break;
    case 0x0d:
        *message = "Passw incorrect";
        break;
    default:
        *message = "Unknown error";
    }
    return TEMProtocol::OTHER_ERROR;
}

uint64_t TMilurProtocol::ReadRegister(uint32_t slave, uint32_t address, RegisterFormat fmt)
{
    int size;
    bool bcd;
    GetRegType(fmt, &size, &bcd);

    uint8_t addr = address;
    uint8_t buf[MAX_LEN], *p = buf;
    Talk(slave, 0x01, &addr, 1, 0x01, buf, size + 2);
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

#if 0
#include <iomanip>
#include <iostream>
int main(int, char**)
{
    try {
        TSerialPortSettings settings("/dev/ttyNSC0", 9600, 'N', 8, 2, 1000);
        PAbstractSerialPort port(new TSerialPort(settings, true));
        port->Open();
        TMilurProtocol milur(port);
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
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
