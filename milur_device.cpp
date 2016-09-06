#include "milur_device.h"
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
        case S16:
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
            throw TSerialDeviceException("milur: unsupported register format");
        }
    }

    TAbstractSerialPort::TFrameCompletePred ExpectNBytes(int n)
    {
        return [n](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (buf[1] & 0x80)
                return size >= 6; // exception response
            return size >= n;
        };
    }
}

REGISTER_BASIC_INT_PROTOCOL("milur", TMilurDevice, TRegisterTypes({
            { TMilurDevice::REG_PARAM, "param", "value", U24, true },
            { TMilurDevice::REG_POWER, "power", "power", S32, true },
            { TMilurDevice::REG_ENERGY, "energy", "power_consumption", BCD32, true },
            { TMilurDevice::REG_FREQ, "freq", "value", BCD32, true },
            { TMilurDevice::REG_POWERFACTOR, "power_factor", "value", S16, true }
        }));

TMilurDevice::TMilurDevice(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol)
    : TEMDevice<TBasicProtocol<TMilurDevice>>(device_config, port, protocol)
{}

bool TMilurDevice::ConnectionSetup(uint8_t slave)
{
    uint8_t setupCmd[7] = {
        // full: 0xff, 0x08, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5f, 0xed
        uint8_t(DeviceConfig()->AccessLevel), 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    std::vector<uint8_t> password = DeviceConfig()->Password;
    if (password.size()) {
        if (password.size() != 6)
            throw TSerialDeviceException("invalid password size (6 bytes expected)");
        std::copy(password.begin(), password.end(), setupCmd + 1);
    }

    uint8_t buf[MAX_LEN];
    WriteCommand(slave, 0x08, setupCmd, 7);
    try {
        if (!ReadResponse(slave, 0x08, buf, 1, ExpectNBytes(5)))
            return false;
        if (buf[0] != uint8_t(DeviceConfig()->AccessLevel))
            throw TSerialDeviceException("invalid milur access level in response");
        return true;
    } catch (TSerialDeviceTransientErrorException&) {
            // retry upon response from a wrong slave
        return false;
    }
}

TEMDevice<TMilurProtocol>::ErrorType TMilurDevice::CheckForException(uint8_t* frame, int len, const char** message)
{
    if (len != 6 || !(frame[1] & 0x80)) {
        *message = 0;
        return TEMDevice<TMilurProtocol>::NO_ERROR;
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
        return TEMDevice<TMilurProtocol>::NO_OPEN_SESSION;
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
    return TEMDevice<TMilurProtocol>::OTHER_ERROR;
}

uint64_t TMilurDevice::ReadRegister(PRegister reg)
{
    int size;
    bool bcd;
    GetRegType(reg->Format, &size, &bcd);

    uint8_t addr = reg->Address;
    uint8_t buf[MAX_LEN], *p = buf;
    Talk(SlaveId, 0x01, &addr, 1, 0x01, buf, size + 2, ExpectNBytes(size + 6));
    if (*p++ != reg->Address)
        throw TSerialDeviceTransientErrorException("bad register address in the response");
    if (*p++ != size)
        throw TSerialDeviceTransientErrorException("bad register size in the response");

    uint64_t r = 0;
    if (bcd) {
        for (int i = 0, mul = 1; i < size; ++i, mul *= 100) {
            int v = buf[i + 2];
            r += ((v & 0x0f) * 10 + (v >> 4)) * mul;
        }
    } else {
        for (int i = 0; i < size; ++i) {
            r += buf[i + 2] << (i * 8);
        }
    }

    return r;
}

void TMilurDevice::Prepare()
{
    /* Milur 104 ignores the request after receiving any packet
    with length of 8 bytes. The last answer of the previously polled device
    could make Milur 104 unresponsive. To make sure the meter is operational,
    we send dummy packet (0xFF in this case) which will restore normal meter operation. */

    uint8_t buf[] = {0xFF};
    Port()->WriteBytes(buf, sizeof(buf) / sizeof(buf[0]));
    TSerialDevice::Prepare();
    Port()->SkipNoise();
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
        TMilurDevice milur(port);
        std::ios::fmtflags f(std::cerr.flags());
        int v = milur.ReadRegister(0xff, 102, 0, U24);
        std::cerr << "value of mod 0xff reg 0x66: 0x" << std::setw(8) << std::hex << v << std::endl;
        std::cerr.flags(f);
        std::cerr << "dec value: " << v << std::endl;

        int v1 = milur.ReadRegister(0xff, 118, 0, BCD32);
        std::cerr << "value of mod 0xff reg 0x76: " << v1 << std::endl;
    } catch (const TSerialDeviceException& e) {
        std::cerr << "milur: " << e.what() << std::endl;
    }
    return 0;
}
#endif

// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
