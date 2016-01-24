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

#include "uniel_protocol.h"

namespace {
    enum {
        READ_CMD           = 0x05,
        WRITE_CMD          = 0x06,
        SET_BRIGHTNESS_CMD = 0x0a
    };
}

REGISTER_PROTOCOL("uniel", TUnielProtocol, TRegisterTypes({
            { TUnielProtocol::REG_RELAY, "relay", "switch", U8 },
            { TUnielProtocol::REG_INPUT, "input", "text", U8, true },
            { TUnielProtocol::REG_PARAM, "param", "value", U8 },
            // "value", not "range" because 'max' cannot be specified here.
            { TUnielProtocol::REG_BRIGHTNESS, "brightness", "value", U8 }
        }));

TUnielProtocol::TUnielProtocol(PDeviceConfig, PAbstractSerialPort port)
    : TSerialProtocol(port) {}

void TUnielProtocol::WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3)
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

void TUnielProtocol::ReadResponse(uint8_t cmd, uint8_t* response)
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
            throw TSerialProtocolTransientErrorException("uniel: warning: checksum failure");

        break;
    }

    if (buf[0] != cmd)
        throw TSerialProtocolTransientErrorException("bad command code in response");

    if (buf[1] != 0)
        throw TSerialProtocolTransientErrorException("bad module address in response");

    for (int i = 2; i < 5; ++i)
        *response++ = buf[i];
}

uint64_t TUnielProtocol::ReadRegister(PRegister reg)
{
    WriteCommand(READ_CMD, reg->Slave, 0, uint8_t(reg->Address), 0);
    uint8_t response[3];
    ReadResponse(READ_CMD, response);
    if (response[1] != uint8_t(reg->Address))
        throw TSerialProtocolTransientErrorException("register index mismatch");

    if (reg->Type == REG_RELAY)
        return response[0] ? 1 : 0;
    return response[0];
}

void TUnielProtocol::WriteRegister(PRegister reg, uint64_t value)
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
    WriteCommand(cmd, reg->Slave, value, addr, 0);
    uint8_t response[3];
    ReadResponse(cmd, response);
    if (response[1] != addr)
        throw TSerialProtocolTransientErrorException("register index mismatch");
    if (response[0] != value)
        throw TSerialProtocolTransientErrorException("written register value mismatch");
}
