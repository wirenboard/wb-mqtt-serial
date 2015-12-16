#include <map>
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

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings), Debug(false), Fd(-1) {}

TSerialPort::~TSerialPort()
{
    if (Fd >= 0)
        close(Fd);
}

void TSerialPort::SetDebug(bool debug)
{
    Debug = debug;
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
        // TBD: move this to libwbmqtt (HexDump?)
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Write:" << std::hex << std::setfill('0');
        for (int i = 0; i < count; ++i)
            std::cerr << " " << std::setw(2) << int(buf[i]);
        std::cerr << std::endl;
        std::cerr.flags(f);
    }
}

bool TSerialPort::Select(int ms)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

#if 0
    // too verbose
    if (Debug)
        std::cerr << "Select: " << ms << " ms" << std::endl;
#endif

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

int TSerialPort::ReadFrame(uint8_t* buf, int size, int timeout)
{
    CheckPortOpen();
    int nread = 0;
    while (nread < size) {
        if (!Select(!nread ? Settings.ResponseTimeoutMs : timeout))
            break; // end of the frame

        // We don't want to use non-blocking IO in general
        // (e.g. we want blocking writes), but we don't want
        // read() call below to block because actual frame
        // size is not known at this point. So we must
        // know how many bytes are available
        int nb;
        if (ioctl(Fd, FIONREAD, &nb) < 0)
            throw TSerialProtocolException("FIONREAD ioctl() failed");
        if (!nb)
            continue; // shouldn't happen, actually
        if (nb > size - nread)
            nb = size - nread;

        int n = read(Fd, buf + nread, nb);
        if (n < 0)
            throw TSerialProtocolException("read() failed");
        if (n < nb) // may happen only due to a kernel/driver bug
            throw TSerialProtocolException("short read()");

        nread += nb;
    }

    if (!nread)
        throw TSerialProtocolException("request timed out");

    if (Debug) {
        // TBD: move this to libwbmqtt (HexDump?)
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "ReadFrame:" << std::hex << std::uppercase << std::setfill('0');
        for (int i = 0; i < nread; ++i) {
            std::cerr << " " << std::setw(2) << int(buf[i]);
        }
        std::cerr << std::endl;
        std::cerr.flags(f);
    }

    return nread;
}

void TSerialPort::SkipNoise()
{
    uint8_t b;
    while (Select(NoiseTimeoutMs)) {
        if (read(Fd, &b, 1) < 1)
            throw TSerialProtocolException("read() failed");
        if (Debug) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "read noise: " << std::hex << std::setfill('0') << std::setw(2) << int(b) << std::endl;
            std::cerr.flags(f);
        }

    }
}

void TSerialPort::USleep(int usec)
{
    usleep(usec);
}

TSerialProtocol::TSerialProtocol(PAbstractSerialPort port)
    : SerialPort(port) {}

TSerialProtocol::~TSerialProtocol() {}

void TSerialProtocol::EndPollCycle() {}

std::unordered_map<std::string, TSerialProtocolFactory::TSerialProtocolMaker>
    *TSerialProtocolFactory::ProtoMakers = 0;

void TSerialProtocolFactory::RegisterProtocol(const std::string& name, TSerialProtocolMaker maker)
{
    if (!ProtoMakers)
        ProtoMakers = new std::unordered_map<std::string, TSerialProtocolFactory::TSerialProtocolMaker>();
    (*ProtoMakers)[name] = maker;
}

PSerialProtocol TSerialProtocolFactory::CreateProtocol(PDeviceConfig device_config, PAbstractSerialPort port)
{
    auto it = ProtoMakers->find(device_config->Protocol);
    if (it == ProtoMakers->end())
        throw TSerialProtocolException("unknown serial protocol");
    return it->second(device_config, port);
}
