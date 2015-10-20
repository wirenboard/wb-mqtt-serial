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

TSerialProtocol::TSerialProtocol(const std::string& device, int timeout_ms)
    : Device(device), TimeoutMs(timeout_ms), Fd(-1) {}

TSerialProtocol::~TSerialProtocol()
{
    if (Fd >= 0)
        close(Fd);
}

void TSerialProtocol::Open()
{
    if (Fd >= 0)
        throw TSerialProtocolException("port already open");

    Fd = open(Device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (Fd < 0) {
        throw TSerialProtocolException("cannot open serial port");
    }
    try {
        SerialPortSetup();
    } catch (const TSerialProtocolException&) {
        close(Fd);
        Fd = -1;
        throw;
    }
}

void TSerialProtocol::Close()
{
    EnsurePortOpen();
    close(Fd);
    Fd = -1;
}

bool TSerialProtocol::IsOpen() const
{
    return Fd >= 0;
}

void TSerialProtocol::SerialPortSetup()
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
        throw TSerialProtocolException("failed to set serial port parameters");
}

void TSerialProtocol::EnsurePortOpen()
{
    if (Fd < 0)
        throw TSerialProtocolException("port not open");
}

uint8_t TSerialProtocol::ReadByte()
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
        throw TSerialProtocolException("select() failed");

    if (!r)
        throw TSerialProtocolTransientErrorException("timeout");

    uint8_t b;
    if (read(Fd, &b, 1) < 1)
        throw TSerialProtocolException("read() failed");

    return b;
        throw TSerialProtocolException("failed to write command");
}
