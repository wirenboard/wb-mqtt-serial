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

#include "uniel_device.h"

namespace {
    enum {
        READ_CMD           = 0x05,
        WRITE_CMD          = 0x06,
        SET_BRIGHTNESS_CMD = 0x0a
    };
}

REGISTER_BASIC_INT_PROTOCOL("uniel", TUnielDevice, TRegisterTypes({
            { TUnielDevice::REG_RELAY, "relay", "switch", U8 },
            { TUnielDevice::REG_INPUT, "input", "text", U8, true },
            { TUnielDevice::REG_PARAM, "param", "value", U8 },
            // "value", not "range" because 'max' cannot be specified here.
            { TUnielDevice::REG_BRIGHTNESS, "brightness", "value", U8 }
        }));

TUnielDevice::TUnielDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol)
    , TBasicProtocolSerialDevice<TBasicProtocol<TUnielDevice>>(config, protocol)
{}

void TUnielDevice::WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3)
{
    Port()->CheckPortOpen();
    uint8_t buf[8];
    buf[0] = buf[1] = 0xff;
    buf[2] = cmd;
    buf[3] = mod;
    buf[4] = b1;
    buf[5] = b2;
    buf[6] = b3;
    buf[7] = (cmd + mod + b1 + b2 + b3) & 0xff;
    Port()->WriteBytes(buf, 8);
}

void TUnielDevice::ReadResponse(uint8_t cmd, uint8_t* response)
{
    uint8_t buf[5];
    for (;;) {
        if (Port()->ReadByte() != 0xff || Port()->ReadByte() != 0xff) {
            std::cerr << "uniel: warning: resync" << std::endl;
            continue;
        }
        uint8_t s = 0;
        for (int i = 0; i < 5; ++i) {
            buf[i] = Port()->ReadByte();
            s += buf[i];
        }

        if (Port()->ReadByte() != s)
            throw TSerialDeviceTransientErrorException("uniel: warning: checksum failure");

        break;
    }

    if (buf[0] != cmd)
        throw TSerialDeviceTransientErrorException("bad command code in response");

    if (buf[1] != 0)
        throw TSerialDeviceTransientErrorException("bad module address in response");

    for (int i = 2; i < 5; ++i)
        *response++ = buf[i];
}

uint64_t TUnielDevice::ReadRegister(PRegister reg)
{
    WriteCommand(READ_CMD, SlaveId, 0, uint8_t(reg->Address), 0);
    uint8_t response[3];
    ReadResponse(READ_CMD, response);
    if (response[1] != uint8_t(reg->Address))
        throw TSerialDeviceTransientErrorException("register index mismatch");

    if (reg->Type == REG_RELAY)
        return response[0] ? 1 : 0;
    return response[0];
}

void TUnielDevice::WriteRegister(PRegister reg, uint64_t value)
{
    uint8_t cmd, addr;
    if (reg->Type == REG_BRIGHTNESS) {
        cmd = SET_BRIGHTNESS_CMD;
        addr = uint8_t(reg->Address >> 8);
    } else {
        cmd = WRITE_CMD;
        addr = uint8_t(reg->Address);
    }
    if (reg->Type == REG_RELAY && value != 0)
        value = 255;
    WriteCommand(cmd, SlaveId, value, addr, 0);
    uint8_t response[3];
    ReadResponse(cmd, response);
    if (response[1] != addr)
        throw TSerialDeviceTransientErrorException("register index mismatch");
    if (response[0] != value)
        throw TSerialDeviceTransientErrorException("written register value mismatch");
}
