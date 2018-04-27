#include "mercury230_device.h"
#include "protocol_register.h"
#include "crc16.h"
#include "protocol_register_bind_info.h"

#include <cassert>
#include <iostream>

REGISTER_BASIC_INT_PROTOCOL("mercury230", TMercury230Device, TRegisterTypes({
            { TMercury230Device::REG_VALUE_ARRAY, "array", "power_consumption", { U32, U32, U32 }, true },
            { TMercury230Device::REG_VALUE_ARRAY12, "array12", "power_consumption", { U32, U32, U32 }, true },
            { TMercury230Device::REG_PARAM, "param", "value", { U24 }, true },
            { TMercury230Device::REG_PARAM_SIGN_ACT, "param_sign_active", "value", { S24 }, true },
            { TMercury230Device::REG_PARAM_SIGN_REACT, "param_sign_reactive", "value", { S24 }, true },
            { TMercury230Device::REG_PARAM_SIGN_IGNORE, "param_sign_ignore", "value", { U24 }, true },
            { TMercury230Device::REG_PARAM_BE, "param_be", "value", { S24 }, true }
        }));

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

struct TIRMercury230DeviceMemoryView: TIRDeviceMemoryView
{
    uint64_t Get(const TIRDeviceValueDesc & valueDesc) const override
    {
        switch (Type.Index) {
        case TMercury230Device::REG_VALUE_ARRAY:
        case TMercury230Device::REG_VALUE_ARRAY12:
            return TIRDeviceMemoryView::Get(valueDesc);
        case TMercury230Device::REG_PARAM_SIGN_ACT:
        case TMercury230Device::REG_PARAM_SIGN_REACT:
        case TMercury230Device::REG_PARAM_SIGN_IGNORE:
        case TMercury230Device::REG_PARAM:
        case TMercury230Device::REG_PARAM_BE:
        {
            assert(valueDesc.BoundMemoryBlocks.size() == 1);
            const auto & memoryBlock = valueDesc.BoundMemoryBlocks.begin()->first;

            auto buf = RawMemory + GetBlockStart(memoryBlock);

            if (memoryBlock->Size == 3) {
                if ((Type.Index == TMercury230Device::REG_PARAM_SIGN_ACT) ||
                    (Type.Index == TMercury230Device::REG_PARAM_SIGN_REACT) ||
                    (Type.Index == TMercury230Device::REG_PARAM_SIGN_IGNORE)
                ) {
                    uint32_t magnitude = (((uint32_t)buf[0] & 0x3f) << 16) +
                                          ((uint32_t)buf[2] << 8) +
                                           (uint32_t)buf[1];

                    int active_power_sign   = (buf[0] & (1 << 7)) ? -1 : 1;
                    int reactive_power_sign = (buf[0] & (1 << 6)) ? -1 : 1;

                    int sign = 1;

                    if (Type.Index == TMercury230Device::REG_PARAM_SIGN_ACT)  {
                            sign = active_power_sign;
                    } else if (Type.Index == TMercury230Device::REG_PARAM_SIGN_REACT) {
                            sign = reactive_power_sign;
                    }

                    return (uint32_t)(((int32_t) magnitude * sign));
                } else {
                    return ((uint32_t)buf[0] << 16) +
                           ((uint32_t)buf[2] << 8) +
                            (uint32_t)buf[1];
                }
            } else  {
                if (Type.Index == TMercury230Device::REG_PARAM_BE) {
                    return ((uint32_t)buf[0] << 8) +
                           ((uint32_t)buf[1]);
                } else {
                    return ((uint32_t)buf[1] << 8) +
                           ((uint32_t)buf[0]);
                }
            }
        }
        default:
            throw TSerialDeviceException("TIRMercury230DeviceMemoryView: invalid register type");
        }
    }
};

const TIRDeviceMemoryView & TMercury230Device::CreateMemoryView(const std::vector<uint8_t> & memory, const PProtocolRegister & memoryBlock)
{
    return TIRMercury230DeviceMemoryView{ memory.data(), memory.size(), memoryBlock->Type, memoryBlock->Address, memoryBlock->Size };
}

std::vector<uint8_t> TMercury230Device::ReadValueArray(const PProtocolRegister & mb)
{
    uint8_t cmdBuf[2];
    cmdBuf[0] = (uint8_t)((mb->Address >> 4) & 0xff); // high nibble = array number, lower nibble = month
    cmdBuf[1] = (uint8_t)((mb->Address >> 12) & 0x0f); // tariff
    std::vector<uint8_t> buf(mb->Size);
    Talk(0x05, cmdBuf, 2, -1, buf.data(), buf.size());

    return buf;
}

std::vector<uint8_t> TMercury230Device::ReadParam(const PProtocolRegister & mb)
{
    uint8_t cmdBuf[2];
    cmdBuf[0] = (mb->Address >> 8) & 0xff; // param
    cmdBuf[1] = mb->Address & 0xff; // subparam (BWRI)

    assert(mb->Size <= 3);
    std::vector<uint8_t> buf(mb->Size);
    Talk( 0x08, cmdBuf, 2, -1, buf.data(), buf.size());
    return buf;
}

std::vector<uint8_t> TMercury230Device::ReadProtocolRegister(const PProtocolRegister & reg)
{
    switch (reg->Type.Index) {
    case REG_VALUE_ARRAY:
        return ReadValueArray(reg);
    case REG_VALUE_ARRAY12:
        return ReadValueArray(reg);
    case REG_PARAM:
    case REG_PARAM_SIGN_ACT:
    case REG_PARAM_SIGN_REACT:
    case REG_PARAM_SIGN_IGNORE:
    case REG_PARAM_BE:
        return ReadParam(reg);
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
