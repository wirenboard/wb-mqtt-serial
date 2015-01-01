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
    : Device(device), TimeoutMs(timeout_ms), Fd(-1) {}

TUnielBus::~TUnielBus()
{
    if (Fd >= 0)
        close(Fd);
}

void TUnielBus::Open()
{
    if (Fd >= 0)
        throw TUnielBusException("port already open");

    Fd = open(Device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (Fd < 0) {
        throw TUnielBusException("cannot open serial port");
    }
    try {
        SerialPortSetup();
    } catch (const TUnielBusException&) {
        close(Fd);
        Fd = -1;
        throw;
    }
}

void TUnielBus::Close()
{
    EnsurePortOpen();
    close(Fd);
    Fd = -1;
}

bool TUnielBus::IsOpen() const
{
    return Fd >= 0;
}

void TUnielBus::SerialPortSetup()
{
    struct termios oldOptions, newOptions;
    tcgetattr(Fd, &oldOptions);
    bzero(&newOptions, sizeof(newOptions));

    // 9600, 8 bit, 1 stop bit, raw mode
    cfsetispeed(&newOptions, B9600);
    cfsetospeed(&newOptions, B9600);

    newOptions.c_cflag &= ~(CRTSCTS | CSIZE | CSTOPB | PARENB | PARODD);
    newOptions.c_cflag |= (CREAD | CLOCAL | CS8);
	newOptions.c_iflag &= ~(IXANY | IXON | IXOFF | INPCK);
    newOptions.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    newOptions.c_oflag &=~ OPOST;
    newOptions.c_cc[VMIN] = 0;
    newOptions.c_cc[VTIME] = 0;
    if (tcsetattr(Fd, TCSANOW, &newOptions) < 0)
        throw TUnielBusException("failed to set serial port parameters");
}

void TUnielBus::EnsurePortOpen()
{
    if (Fd < 0)
        throw TUnielBusException("port not open");
}

void TUnielBus::WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3)
{
    EnsurePortOpen();
    unsigned char buf[8];
    buf[0] = buf[1] = 0xff;
    buf[2] = cmd;
    buf[3] = mod;
    buf[4] = b1;
    buf[5] = b2;
    buf[6] = b3;
    buf[7] = (cmd + mod + b1 + b2 + b3) & 0xff;
    if (write(Fd, buf, 8) < 8)
        throw TUnielBusException("failed to write command");
}

uint8_t TUnielBus::ReadByte()
{
    EnsurePortOpen();

    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);

    tv.tv_sec = TimeoutMs / 1000;
    tv.tv_usec = (TimeoutMs % 1000) * 1000;

    int r = select(Fd + 1, &rfds, NULL, NULL, &tv);
    if (r < 0)
        throw TUnielBusException("select() failed");

    if (!r)
        throw TUnielBusTransientErrorException("timeout");

    uint8_t b;
    if (read(Fd, &b, 1) < 1)
        throw TUnielBusException("read() failed");

    return b;
}

void TUnielBus::ReadResponse(uint8_t cmd, uint8_t* response)
{
    unsigned char buf[5];
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
            throw TUnielBusTransientErrorException("uniel: warning: checksum failure");

        break;
    }

    if (buf[0] != cmd)
        throw TUnielBusTransientErrorException("bad command code in response");

    if (buf[1] != 0)
        throw TUnielBusTransientErrorException("bad module address in response");

    for (int i = 2; i < 5; ++i)
        *response++ = buf[i];
}

uint8_t TUnielBus::ReadRegister(uint8_t mod, uint8_t address)
{
    WriteCommand(READ_CMD, mod, 0, address, 0);
    uint8_t response[3];
    ReadResponse(READ_CMD, response);
    if (response[1] != address)
        throw TUnielBusTransientErrorException("register index mismatch");

    return response[0];
}

void TUnielBus::DoWriteRegister(uint8_t cmd, uint8_t mod, uint8_t address, uint8_t value)
{
    WriteCommand(cmd, mod, value, address, 0);
    uint8_t response[3];
    ReadResponse(cmd, response);
    if (response[1] != address)
        throw TUnielBusTransientErrorException("register index mismatch");
    if (response[0] != value)
        throw TUnielBusTransientErrorException("written register value mismatch");
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
