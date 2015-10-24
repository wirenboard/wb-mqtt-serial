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
#include <iomanip>

#include "serial_protocol.h"

TAbstractSerialPort::~TAbstractSerialPort() {}

TSerialPort::TSerialPort(const TSerialPortSettings& settings, bool debug)
    : Settings(settings), Debug(debug), Fd(-1) {}

TSerialPort::~TSerialPort()
{
    if (Fd >= 0)
        close(Fd);
}

void TSerialPort::Open()
{
    if (Fd >= 0)
        throw TSerialProtocolException("port already open");

    Fd = open(Settings.Device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
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

void TSerialPort::Close()
{
    CheckPortOpen();
    close(Fd);
    Fd = -1;
}

bool TSerialPort::IsOpen() const
{
    return Fd >= 0;
}

void TSerialPort::SerialPortSetup()
{
    int speed;
    struct termios oldOptions, newOptions;
    if (tcgetattr(Fd, &oldOptions) < 0)
        throw TSerialProtocolException("tcgetattr() failed");
    bzero(&newOptions, sizeof(newOptions));

    switch (Settings.BaudRate) {
    case 110:
        speed = B110;
        break;
    case 300:
        speed = B300;
        break;
    case 600:
        speed = B600;
        break;
    case 1200:
        speed = B1200;
        break;
    case 2400:
        speed = B2400;
        break;
    case 4800:
        speed = B4800;
        break;
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    default:
        throw TSerialProtocolException("bad baud rate value for the port");
    }

    if (cfsetispeed(&newOptions, speed) < 0 ||
        cfsetospeed(&newOptions, speed) < 0)
        throw TSerialProtocolException("failed to set serial port baud rate");

    newOptions.c_cflag &= ~(CRTSCTS | CSIZE | CSTOPB | PARENB | PARODD);
    newOptions.c_cflag |= (CREAD | CLOCAL);
	newOptions.c_iflag &= ~(IXANY | IXON | IXOFF | INPCK);

    switch (Settings.DataBits) {
    case 5:
        newOptions.c_cflag |= CS5;
        break;
    case 6:
        newOptions.c_cflag |= CS6;
        break;
    case 7:
        newOptions.c_cflag |= CS7;
        break;
    default:
        newOptions.c_cflag |= CS8;
        break;
    }

    if (Settings.StopBits == 1)
        newOptions.c_cflag &=~ CSTOPB;
    else // 2 stop bits
        newOptions.c_cflag |= CSTOPB;

    if (Settings.Parity == 'N')
        newOptions.c_cflag &=~ PARENB; // no parity
    else if (Settings.Parity == 'E') {
        // even parity
        newOptions.c_cflag |= PARENB;
        newOptions.c_cflag &=~ PARODD;
        newOptions.c_iflag |= INPCK;
    } else {
        // odd parity
        newOptions.c_cflag |= PARENB;
        newOptions.c_cflag |= PARODD;
        newOptions.c_iflag |= INPCK;
    }

    newOptions.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    newOptions.c_oflag &=~ OPOST;
    newOptions.c_cc[VMIN] = 0;
    newOptions.c_cc[VTIME] = 0;
    if (tcsetattr(Fd, TCSANOW, &newOptions) < 0)
        throw TSerialProtocolException("failed to set serial port parameters");
}

void TSerialPort::CheckPortOpen()
{
    if (Fd < 0)
        throw TSerialProtocolException("port not open");
}

void TSerialPort::WriteBytes(const uint8_t* buf, int count) {
    if (write(Fd, buf, count) < count)
        throw TSerialProtocolException("serial write failed");
    if (Debug) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Write:";
        for (int i = 0; i < count; ++i)
            std::cerr << " " << std::hex << std::setw(2) << std::setfill('0') << int(buf[i]);
        std::cerr << std::endl;
        std::cerr.flags(f);
    }
}

bool TSerialPort::Select(int ms)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

    if (Debug)
        std::cerr << "Select: " << ms << " ms" << std::endl;

    if (ms > 0) {
        FD_ZERO(&rfds);
        FD_SET(Fd, &rfds);

        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        tvp = &tv;
    }

    int r = select(Fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0)
        throw TSerialProtocolException("select() failed");

    return r > 0;
}

uint8_t TSerialPort::ReadByte()
{
    CheckPortOpen();

    if (!Select(Settings.ResponseTimeoutMs))
        throw TSerialProtocolTransientErrorException("timeout");

    uint8_t b;
    if (read(Fd, &b, 1) < 1)
        throw TSerialProtocolException("read() failed");

    if (Debug) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Read: " << std::hex << std::setw(2) << std::setfill('0') << int(b) << std::endl;
        std::cerr.flags(f);
    }

    return b;
}

void TSerialPort::SkipNoise()
{
    uint8_t b;
    while (Select(NoiseTimeoutMs)) {
        if (read(Fd, &b, 1) < 1)
            throw TSerialProtocolException("read() failed");
    }
}

TSerialProtocol::TSerialProtocol(PAbstractSerialPort port)
    : SerialPort(port) {}

TSerialProtocol::~TSerialProtocol() {}

void TSerialProtocol::EndPollCycle() {}
