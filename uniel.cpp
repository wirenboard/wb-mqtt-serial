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

#include "uniel.h"

namespace {
    enum {
        READ_CMD           = 0x05,
        WRITE_CMD          = 0x06,
        SET_BRIGHTNESS_CMD = 0x0a
    };
}

TUnielBus::TUnielBus(const std::string& device, int timeout_ms)
    : TSerialProtocol(device, timeout_ms) {}

void TUnielBus::WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3)
{
    EnsurePortOpen();
    uint8_t buf[8];
    buf[0] = buf[1] = 0xff;
    buf[2] = cmd;
    buf[3] = mod;
    buf[4] = b1;
    buf[5] = b2;
    buf[6] = b3;
    buf[7] = (cmd + mod + b1 + b2 + b3) & 0xff;
    if (write(Fd, buf, 8) < 8)
        throw TSerialProtocolException("failed to write command");
}

void TUnielBus::ReadResponse(uint8_t cmd, uint8_t* response)
{
    uint8_t buf[5];
    for (;;) {
        if (ReadByte() != 0xff || ReadByte() != 0xff) {
            std::cerr << "uniel: warning: resync" << std::endl;
            continue;
        }
        uint8_t s = 0;
        for (int i = 0; i < 5; ++i) {
            buf[i] = ReadByte();
            s += buf[i];
        }

        if (ReadByte() != s)
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

uint8_t TUnielBus::ReadRegister(uint8_t mod, uint8_t address)
{
    WriteCommand(READ_CMD, mod, 0, address, 0);
    uint8_t response[3];
    ReadResponse(READ_CMD, response);
    if (response[1] != address)
        throw TSerialProtocolTransientErrorException("register index mismatch");

    return response[0];
}

void TUnielBus::DoWriteRegister(uint8_t cmd, uint8_t mod, uint8_t address, uint8_t value)
{
    WriteCommand(cmd, mod, value, address, 0);
    uint8_t response[3];
    ReadResponse(cmd, response);
    if (response[1] != address)
        throw TSerialProtocolTransientErrorException("register index mismatch");
    if (response[0] != value)
        throw TSerialProtocolTransientErrorException("written register value mismatch");
}

void TUnielBus::WriteRegister(uint8_t mod, uint8_t address, uint8_t value)
{
    DoWriteRegister(WRITE_CMD, mod, address, value);
}

void TUnielBus::SetBrightness(uint8_t mod, uint8_t address, uint8_t value)
{
    DoWriteRegister(SET_BRIGHTNESS_CMD, mod, address, value);
}

#if 0
int main(int, char**)
{
    try {
        TUnielBus bus("/dev/ttyNSC1");
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
    } catch (const TUnielBusException& e) {
        std::cerr << "uniel bus error: " << e.what() << std::endl;
    }
    return 0;
}
#endif
