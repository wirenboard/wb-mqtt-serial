#include "mercury230_device.h"
#include "memory_block.h"
#include "ir_device_query.h"
#include "crc16.h"
#include "ir_bind_info.h"

#include <cassert>
#include <iostream>

REGISTER_BASIC_INT_PROTOCOL("mercury230", TMercury230Device, TRegisterTypes({
    { TMercury230Device::REG_VALUE_ARRAY, "array", "power_consumption", { U32, U32, U32, U32 }, true },
    { TMercury230Device::REG_VALUE_ARRAY12, "array12", "power_consumption", { U32, U32, U32 }, true },
    { TMercury230Device::REG_PARAM, "param", "value", {}, true, EByteOrder::LittleEndian },
    { TMercury230Device::REG_PARAM_SIGN_ACT, "param_sign_active", "value", { S24 }, true },
    { TMercury230Device::REG_PARAM_SIGN_REACT, "param_sign_reactive", "value", { S24 }, true },
    { TMercury230Device::REG_PARAM_SIGN_IGNORE, "param_sign_ignore", "value", { U24 }, true },
    { TMercury230Device::REG_PARAM_BE, "param_be", "value", {}, true }
}));

const auto MAX_ARRAY_LEN = 16;   // largest array is REG_VALUE_ARRAY 4 uint32

TMercury230Device::TMercury230Device(PDeviceConfig device_config, PPort port, PProtocol protocol)
    : TEMDevice<TBasicProtocol<TMercury230Device>>(device_config, port, protocol)
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
    } catch (TSerialDevicePermanentErrorException&) {
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
        return TEMDevice<TMercury230Protocol>::PERMANENT_ERROR;
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

void TMercury230Device::ReadValueArray(const TIRDeviceQuery & query)
{
    const auto & mb = query.MemoryBlockRange.GetFirst();

    uint8_t cmdBuf[2];
    cmdBuf[0] = (uint8_t)((mb->Address) & 0xff); // high nibble = array number, lower nibble = month
    cmdBuf[1] = (uint8_t)((mb->Address >> 8) & 0x0f); // tariff
    uint8_t buf[MAX_ARRAY_LEN], *p = buf;
    Talk(0x05, cmdBuf, 2, -1, buf, mb->Size);

    const auto & memoryView = query.CreateMemoryView(buf, mb->Size);
    const auto & memoryBlockView = memoryView[mb];

    for (int i = 0; i < mb->Size / 4; i++, p += 4) {
        // assign normalized value
        memoryBlockView[i] = ((uint32_t)p[1] << 24) +
                             ((uint32_t)p[0] << 16) +
                             ((uint32_t)p[3] << 8 ) +
                              (uint32_t)p[2];
    }

    query.FinalizeRead(memoryView);
}

void TMercury230Device::ReadParam(const TIRDeviceQuery & query)
{
    const auto & mb = query.MemoryBlockRange.GetFirst();
    const auto typeIndex = mb->Type.Index;

    uint8_t cmdBuf[2];
    cmdBuf[0] = (mb->Address >> 8) & 0xff; // param
    cmdBuf[1] = mb->Address & 0xff; // subparam (BWRI)

    assert(mb->Size <= 3);
    uint8_t buf[3] = {0};
    Talk( 0x08, cmdBuf, 2, -1, buf, mb->Size);

    if (mb->Size == 3) {
        const auto & memoryView = query.CreateMemoryView(buf, 3);

        if ((typeIndex == TMercury230Device::REG_PARAM_SIGN_ACT) ||
            (typeIndex == TMercury230Device::REG_PARAM_SIGN_REACT) ||
            (typeIndex == TMercury230Device::REG_PARAM_SIGN_IGNORE)
        ) {
            uint32_t magnitude = (((uint32_t)buf[0] & 0x3f) << 16) +
                                  ((uint32_t)buf[2] << 8) +
                                   (uint32_t)buf[1];

            int active_power_sign   = (buf[0] & (1 << 7)) ? -1 : 1;
            int reactive_power_sign = (buf[0] & (1 << 6)) ? -1 : 1;

            int sign = 1;

            if (typeIndex == TMercury230Device::REG_PARAM_SIGN_ACT)  {
                sign = active_power_sign;
            } else if (typeIndex == TMercury230Device::REG_PARAM_SIGN_REACT) {
                sign = reactive_power_sign;
            }

            // assign normalized value
            memoryView[mb] = (uint32_t)(((int32_t) magnitude * sign));
        } else {
            // assign normalized value
            memoryView[mb] = ((uint32_t)buf[0] << 16) +
                             ((uint32_t)buf[2] << 8) +
                              (uint32_t)buf[1];
        }

        query.FinalizeRead(memoryView);
    } else  {
        query.FinalizeRead(buf, 2);
    }
}

void TMercury230Device::Read(const TIRDeviceQuery & query)
{
    switch (query.GetType().Index) {
    case REG_VALUE_ARRAY:
    case REG_VALUE_ARRAY12:
        return ReadValueArray(query);
    case REG_PARAM:
    case REG_PARAM_SIGN_ACT:
    case REG_PARAM_SIGN_REACT:
    case REG_PARAM_SIGN_IGNORE:
    case REG_PARAM_BE:
        return ReadParam(query);
    default:
        throw TSerialDeviceException("mercury230 Read: invalid register type");
    }
}

// TBD: custom password?
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
