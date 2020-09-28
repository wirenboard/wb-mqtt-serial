#include "serial_exc.h"
#include "serial_port.h"
#include "log.h"

#include <string.h>
#include <fcntl.h>
#include <iostream>
#include <utility>
#include <cmath>

#include <linux/serial.h>
#include <sys/ioctl.h>

#define LOG(logger) ::logger.Log() << "[serial port] "

using namespace WBMQTT;

namespace {
    int ConvertBaudRate(int rate)
    {
        switch (rate) {
        case 110:   return B110;
        case 300:   return B300;
        case 600:   return B600;
        case 1200:  return B1200;
        case 2400:  return B2400;
        case 4800:  return B4800;
        case 9600:  return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default:
            LOG(Warn) << "unsupported baud rate " << rate << " defaulting to 9600";
            return B9600;
        }
    }

    int ConvertDataBits(int data_bits)
    {
        switch (data_bits) {
        case 5: return CS5;
        case 6: return CS6;
        case 7: return CS7;
        case 8: return CS8;
        default:
            LOG(Warn) << "unsupported data bits count " << data_bits << " defaulting to 8";
            return CS8;
        }
    }

    std::chrono::milliseconds GetLinuxLag(int baudRate)
    {
        return std::chrono::milliseconds(baudRate < 9600 ? 34 : 24);
    }
};

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings)
{
    memset(&OldTermios, 0, sizeof(termios));
}

void TSerialPort::Open()
{
    if (IsOpen())
        throw TSerialDeviceException("port already open");

    Fd = open(Settings.Device.c_str(), O_RDWR | O_NOCTTY | O_EXCL | O_NDELAY);
    if (Fd < 0)
        throw TSerialDeviceException("cannot open serial port");

    termios dev;
    memset(&dev, 0, sizeof(termios));

    auto baud_rate = ConvertBaudRate(Settings.BaudRate);
    if (cfsetospeed(&dev, baud_rate) != 0 || cfsetispeed(&dev, baud_rate) != 0) {
        auto error_code = errno;
        Close();
        throw TSerialDeviceException("cannot open serial port: error " + std::to_string(error_code) + " from cfsetospeed / cfsetispeed; baud rate is " + std::to_string(Settings.BaudRate));
    }

    if (Settings.StopBits == 1) {
        dev.c_cflag &= ~CSTOPB;
    } else {
        dev.c_cflag |= CSTOPB;
    }

    switch (Settings.Parity) {
    case 'N':
        dev.c_cflag &= ~PARENB;
        dev.c_iflag &= ~INPCK;
        break;
    case 'E':
        dev.c_cflag |= PARENB;
        dev.c_cflag &= ~PARODD;
        dev.c_iflag |= INPCK;
        break;
    case 'O':
        dev.c_cflag |= PARENB;
        dev.c_cflag |= PARODD;
        dev.c_iflag |= INPCK;
        break;
    default:
        Close();
        throw TSerialDeviceException("cannot open serial port: invalid parity value: '" + std::string(1, Settings.Parity) + "'");
    }

    dev.c_cflag = (dev.c_cflag & ~CSIZE) | ConvertDataBits(Settings.DataBits) | CREAD | CLOCAL;
    dev.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    dev.c_iflag &= ~(IXON | IXOFF | IXANY);
    dev.c_oflag &=~ OPOST;
    dev.c_cc[VMIN] = 0;
    dev.c_cc[VTIME] = 0;

    if (tcgetattr(Fd, &OldTermios) != 0) {
        auto error_code = errno;
        Close();
        throw TSerialDeviceException("cannot open serial port: error " + std::to_string(error_code) + " from tcgetattr");
    }

    if (tcsetattr (Fd, TCSANOW, &dev) != 0) {
        auto error_code = errno;
        Close();
        throw TSerialDeviceException("cannot open serial port: error " + std::to_string(error_code) + " from tcsetattr");
    }

    LastInteraction = std::chrono::steady_clock::now();
    SkipNoise();    // flush data from previous instance if any
}

void TSerialPort::Close()
{
    if (Base::IsOpen()) {
        tcsetattr(Fd, TCSANOW, &OldTermios);
    }
    Base::Close();
}

std::chrono::milliseconds TSerialPort::GetSendTime(double bytesNumber)
{
    size_t bitsPerByte = 1 + Settings.DataBits + Settings.StopBits;
    if (Settings.Parity != 'N') {
        ++bitsPerByte;
    }
    auto ms = std::ceil((1000.0*bitsPerByte*bytesNumber)/double(Settings.BaudRate));
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ms));
}

uint8_t TSerialPort::ReadByte(const std::chrono::microseconds& timeout)
{
    return Base::ReadByte(timeout + GetLinuxLag(Settings.BaudRate));
}

size_t TSerialPort::ReadFrame(uint8_t* buf,
                           size_t count,
                           const std::chrono::microseconds& responseTimeout,
                           const std::chrono::microseconds& frameTimeout,
                           TFrameCompletePred frameComplete)
{
    return Base::ReadFrame(buf,
                           count,
                           responseTimeout + GetLinuxLag(Settings.BaudRate),
                           frameTimeout + std::chrono::milliseconds(15),
                           frameComplete);
}

void TSerialPort::WriteBytes(const uint8_t* buf, int count)
{
    Base::WriteBytes(buf, count);
    SleepSinceLastInteraction(GetSendTime(count));
    LastInteraction = std::chrono::steady_clock::now();
}

std::string TSerialPort::GetDescription() const
{
    return Settings.ToString();
}
