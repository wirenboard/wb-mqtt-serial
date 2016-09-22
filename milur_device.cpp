#include "milur_device.h"
#include "bcd_utils.h"

namespace {

    TAbstractSerialPort::TFrameCompletePred ExpectNBytes(int slave_id_width, int n)
    {
        return [slave_id_width, n](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (buf[slave_id_width] & 0x80)
                return size >= 5 + slave_id_width; // exception response
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
{
    /* FIXME: Milur driver should set address width based on slave_id string:
    0xFF: 1-byte address
    255: 1-byte address
    163050000049932: parse as serial number, use 4-byte addressing with slave id = 49932
    */

    if (SlaveId > 0xFF) {
        SlaveIdWidth = 4;
    }
}

bool TMilurDevice::ConnectionSetup()
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
    WriteCommand(0x08, setupCmd, 7);
    try {
        if (!ReadResponse(0x08, buf, 1, ExpectNBytes(SlaveIdWidth, 5)))
            return false;
        if (buf[0] != uint8_t(DeviceConfig()->AccessLevel))
            throw TSerialDeviceException("invalid milur access level in response");
        return true;
    } catch (TSerialDeviceTransientErrorException &) {
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
        *message = "Passwd incorrect";
        break;
    default:
        *message = "Unknown error";
    }
    return TEMDevice<TMilurProtocol>::OTHER_ERROR;
}

uint64_t TMilurDevice::ReadRegister(PRegister reg)
{
    int size = GetExpectedSize(reg->Type);
    uint8_t addr = static_cast<uint8_t>(reg->Address);
    uint8_t buf[MAX_LEN], *p = buf;
    Talk(0x01, &addr, 1, 0x01, buf, size + 2, ExpectNBytes(SlaveIdWidth, size + 5 + SlaveIdWidth));
    if (*p++ != reg->Address)
        throw TSerialDeviceTransientErrorException("bad register address in the response");
    if (*p != size)
        throw TSerialDeviceTransientErrorException("bad register size in the response");

    switch (reg->Type) {
    case TMilurDevice::REG_PARAM:
        return BuildIntVal(buf + 2, 3);
    case TMilurDevice::REG_POWER:
        return BuildIntVal(buf + 2, 4);
    case TMilurDevice::REG_ENERGY:
    case TMilurDevice::REG_FREQ:
        return BuildBCB32(buf + 2);
    case TMilurDevice::REG_POWERFACTOR:
        return BuildIntVal(buf + 2, 2);
    default:
        throw TSerialDeviceTransientErrorException("bad register type");
        }
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

uint64_t TMilurDevice::BuildIntVal(uint8_t *p, int sz) const
{
    uint64_t r = 0;
    for (int i = 0; i < sz; ++i) {
        r += p[i] << (i * 8);
    }
    return r;
}

// We transfer BCD byte arrays as unsigned little endian integers with swapped nibbles.
// To convert it to our standard transport BCD representation (ie. integer with hexadecimal
// that reads exactly as original BCD if printed) we just have to swap nibbles of each byte
// that is decimal value 87654321 comes as {0x12, 0x34, 0x56, 0x78} and becomes {0x21, 0x43, 0x65, 0x87}.
uint64_t TMilurDevice::BuildBCB32(uint8_t *psrc) const
{
    uint32_t r = 0;
    uint8_t *pdst = reinterpret_cast<uint8_t *>(&r);
    for (int i = 0; i < 4; ++i) {
        auto t = psrc[i];
        pdst[i] = (t >> 4) | (t << 4);
    }
    return r;
}

int TMilurDevice::GetExpectedSize(int type) const
{
    auto t = static_cast<TMilurDevice::RegisterType>(type);
    switch (t) {
    case TMilurDevice::REG_PARAM:
        return 3;
    case TMilurDevice::REG_POWER:
    case TMilurDevice::REG_ENERGY:
    case TMilurDevice::REG_FREQ:
        return 4;
    case TMilurDevice::REG_POWERFACTOR:
        return 2;
    default:
        throw TSerialDeviceTransientErrorException("bad register type");
    }
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
