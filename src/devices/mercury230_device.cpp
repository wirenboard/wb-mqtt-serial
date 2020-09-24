#include <cassert>
#include <iostream>
#include "mercury230_device.h"
#include "crc16.h"

REGISTER_BASIC_PROTOCOL("mercury230", TMercury230Device, TRegisterTypes({
            { TMercury230Device::REG_VALUE_ARRAY, "array", "power_consumption", U32, true },
            { TMercury230Device::REG_VALUE_ARRAY12, "array12", "power_consumption", U32, true },
            { TMercury230Device::REG_PARAM, "param", "value", U24, true },
            { TMercury230Device::REG_PARAM_SIGN_ACT, "param_sign_active", "value", S24, true },
            { TMercury230Device::REG_PARAM_SIGN_REACT, "param_sign_reactive", "value", S24, true },
            { TMercury230Device::REG_PARAM_SIGN_IGNORE, "param_sign_ignore", "value", U24, true },
            { TMercury230Device::REG_PARAM_BE, "param_be", "value", S24, true }
        }));

TMercury230Device::TMercury230Device(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TEMDevice(device_config, port, protocol)
{}

bool TMercury230Device::ConnectionSetup( )
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
    WriteCommand(0x01, setupCmd, 7);
    try {
        return ReadResponse(0x00, buf, 0);
    } catch (TSerialDeviceTransientErrorException&) {
            // retry upon response from a wrong slave
        return false;
    } catch (TSerialDevicePermanentRegisterException&) {
    	return false;
    }
}

TEMDevice::ErrorType TMercury230Device::CheckForException(uint8_t* frame, int len, const char** message)
{
    *message = 0;
    if (len != 4 || (frame[1] & 0x0f) == 0)
        return TEMDevice::NO_ERROR;
    switch (frame[1] & 0x0f) {
    case 1:
        *message = "Invalid command or parameter";
        return TEMDevice::PERMANENT_ERROR;
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
        return TEMDevice::NO_OPEN_SESSION;
    default:
        *message = "Unknown error";
    }
    return TEMDevice::OTHER_ERROR;
}

const TMercury230Device::TValueArray& TMercury230Device::ReadValueArray(uint32_t address, int resp_len)
{
    int key = (address >> 4) | (SlaveId << 24);
    auto it = CachedValues.find(key);
    if (it != CachedValues.end())
        return it->second;

    uint8_t cmdBuf[2];
    cmdBuf[0] = (uint8_t)((address >> 4) & 0xff); // high nibble = array number, lower nibble = month
    cmdBuf[1] = (uint8_t)((address >> 12) & 0x0f); // tariff
    uint8_t buf[MAX_LEN], *p = buf;
    TValueArray a;
    Talk(0x05, cmdBuf, 2, -1, buf, resp_len * 4);
    for (int i = 0; i < resp_len; i++, p += 4) {
        a.values[i] = ((uint32_t)p[1] << 24) +
                      ((uint32_t)p[0] << 16) +
                      ((uint32_t)p[3] << 8 ) +
                       (uint32_t)p[2];
    }

    return CachedValues.insert(std::make_pair(key, a)).first->second;
}

uint32_t TMercury230Device::ReadParam( uint32_t address, unsigned resp_payload_len, RegisterType reg_type)
{
    uint8_t cmdBuf[2];
    cmdBuf[0] = (address >> 8) & 0xff; // param
    cmdBuf[1] = address & 0xff; // subparam (BWRI)

    assert(resp_payload_len <= 3);
    uint8_t buf[3] = {};
    Talk( 0x08, cmdBuf, 2, -1, buf, resp_payload_len);

    if (resp_payload_len == 3) {
        if ((reg_type == REG_PARAM_SIGN_ACT) || (reg_type == REG_PARAM_SIGN_REACT) || (reg_type == REG_PARAM_SIGN_IGNORE)) {
            uint32_t magnitude = (((uint32_t)buf[0] & 0x3f) << 16) +
                                ((uint32_t)buf[2] << 8) +
                                (uint32_t)buf[1];

            int active_power_sign =   (buf[0] & (1 << 7)) ? -1 : 1;
            int reactive_power_sign = (buf[0] & (1 << 6)) ? -1 : 1;

            int sign = 1;

            if (reg_type == REG_PARAM_SIGN_ACT) {
                    sign = active_power_sign;
            } else if (reg_type == REG_PARAM_SIGN_REACT) {
                    sign = reactive_power_sign;
            }

            return (uint32_t)(((int32_t) magnitude * sign));
        } else {
            return ((uint32_t)buf[0] << 16) +
                   ((uint32_t)buf[2] << 8) +
                   (uint32_t)buf[1];
        }
    } else  {
        if (reg_type == REG_PARAM_BE) {
            return ((uint32_t)buf[0] << 8) +
                   ((uint32_t)buf[1]);
        } else {
            return ((uint32_t)buf[1] << 8) +
                   ((uint32_t)buf[0]);
       }
    }
}

uint64_t TMercury230Device::ReadRegister(PRegister reg)
{
    switch (reg->Type) {
    case REG_VALUE_ARRAY:
        return ReadValueArray(reg->Address, 4).values[reg->Address & 0x03];
    case REG_VALUE_ARRAY12:
        return ReadValueArray(reg->Address, 3).values[reg->Address & 0x03];
    case REG_PARAM:
    case REG_PARAM_SIGN_ACT:
    case REG_PARAM_SIGN_REACT:
    case REG_PARAM_SIGN_IGNORE:
    case REG_PARAM_BE:
        return ReadParam( reg->Address & 0xffff, reg->ByteWidth(), (RegisterType) reg->Type);
    default:
        throw TSerialDeviceException("mercury230: invalid register type");
    }
}

void TMercury230Device::EndPollCycle()
{
    CachedValues.clear();
    TSerialDevice::EndPollCycle();
}

// TBD: custom password?
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
