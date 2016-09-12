#include <iostream>
#include "mercury230_device.h"
#include "crc16.h"

REGISTER_BASIC_INT_PROTOCOL("mercury230", TMercury230Device, TRegisterTypes({
            { TMercury230Device::REG_VALUE_ARRAY, "array", "power_consumption", U32, true },
            { TMercury230Device::REG_PARAM, "param", "value", U24, true }
        }));

TMercury230Device::TMercury230Device(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol)
    : TEMDevice<TBasicProtocol<TMercury230Device>>(device_config, port, protocol)
{}

bool TMercury230Device::ConnectionSetup(uint8_t slave)
{
    uint8_t setupCmd[7] = {
        uint8_t(DeviceConfig()->AccessLevel), 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
    };

    std::vector<uint8_t> password = DeviceConfig()->Password;
    if (password.size()) {
        if (password.size() != 6)
            throw TSerialDeviceException("invalid password size (6 bytes expected)");
        std::copy(password.begin(), password.end(), setupCmd + 1);
    }

    uint8_t buf[1];
    WriteCommand(slave, 0x01, setupCmd, 7);
    try {
        return ReadResponse(slave, 0x00, buf, 0);
    } catch (TSerialDeviceTransientErrorException&) {
            // retry upon response from a wrong slave
        return false;
    }
}

TEMDevice<TMercury230Protocol>::ErrorType TMercury230Device::CheckForException(uint8_t* frame, int len, const char** message)
{
    *message = 0;
    if (len != 4 || (frame[1] & 0x0f) == 0)
        return TEMDevice<TMercury230Protocol>::NO_ERROR;
    switch (frame[1] & 0x0f) {
    case 1:
        *message = "Invalid command or parameter";
        break;
    case 2:
        *message = "Internal meter error";
        break;
    case 3:
        *message = "Insufficient access level";
        break;
    case 4:
        *message = "Can't correct the clock more than once per day";
        break;
    case 5:
        *message = "Connection closed";
        return TEMDevice<TMercury230Protocol>::NO_OPEN_SESSION;
    default:
        *message = "Unknown error";
    }
    return TEMDevice<TMercury230Protocol>::OTHER_ERROR;
}

const TMercury230Device::TValueArray& TMercury230Device::ReadValueArray(uint32_t slave, uint32_t address)
{
    int key = (address >> 4) || (slave << 24);
    auto it = CachedValues.find(key);
    if (it != CachedValues.end())
        return it->second;

    uint8_t cmdBuf[2];
    cmdBuf[0] = (address >> 4) & 0xff; // high nibble = array number, lower nibble = month
    cmdBuf[1] = (address >> 12) & 0x0f; // tariff
    uint8_t buf[MAX_LEN], *p = buf;
    TValueArray a;
    Talk(slave, 0x05, cmdBuf, 2, -1, buf, 16);
    for (int i = 0; i < 4; i++, p += 4) {
        a.values[i] = ((uint32_t)p[1] << 24) +
                      ((uint32_t)p[0] << 16) +
                      ((uint32_t)p[3] << 8 ) +
                       (uint32_t)p[2];
    }

    return CachedValues.insert(std::make_pair(key, a)).first->second;
}

uint32_t TMercury230Device::ReadParam(uint32_t slave, uint32_t address)
{
    uint8_t cmdBuf[2];
    cmdBuf[0] = (address >> 8) & 0xff; // param
    cmdBuf[1] = address & 0xff; // subparam (BWRI)
    uint8_t subparam = (address & 0xff) >> 4;
    bool isPowerOrPowerCoef = subparam == 0x00 || subparam == 0x03;
    uint8_t buf[3];
    Talk(slave, 0x08, cmdBuf, 2, -1, buf, 3);
    return (((uint32_t)buf[0] & (isPowerOrPowerCoef ? 0x3f : 0xff)) << 16) +
            ((uint32_t)buf[2] << 8) +
             (uint32_t)buf[1];
}

uint64_t TMercury230Device::ReadRegister(PRegister reg)
{
    switch (reg->Type) {
    case REG_VALUE_ARRAY:
        return ReadValueArray(SlaveId, reg->Address).values[reg->Address & 0x03];
    case REG_PARAM:
        return ReadParam(SlaveId, reg->Address & 0xffff);
    default:
        throw TSerialDeviceException("mercury230: invalid register type");
    }
}

void TMercury230Device::EndPollCycle()
{
    CachedValues.clear();
}

// TBD: custom password?
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
