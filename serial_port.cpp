#include <map>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <utility>


#include "serial_exc.h"
#include "serial_port.h"

namespace {
    const std::chrono::milliseconds DefaultFrameTimeout(15);
    const std::chrono::milliseconds NoiseTimeout(10);

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
            std::cerr << "Warning: unsupported baud rate " << rate << " defaulting to 9600" << std::endl;
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
            std::cerr << "Warning: unsupported data bits count " << data_bits << " defaulting to 8" << std::endl;
            return CS8;
        }
    }
};


TAbstractSerialPort::~TAbstractSerialPort() {}

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings),
      Dbg(false),
      Fd(-1) 
{
    memset(&OldTermios, 0, sizeof(termios));
}

TSerialPort::~TSerialPort()
{
    if (Fd >= 0)
        close(Fd);
}

void TSerialPort::SetDebug(bool debug)
{
    Dbg = debug;
}

bool TSerialPort::Debug() const
{
    return Dbg;
}

void TSerialPort::Open()
{
    if (Fd >= 0)
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
}

void TSerialPort::Close()
{
    CheckPortOpen();
    tcsetattr(Fd, TCSANOW, &OldTermios);
    close(Fd);
    Fd = -1;
}

bool TSerialPort::IsOpen() const
{
    return Fd >= 0;
}

void TSerialPort::CheckPortOpen()
{
    if (Fd < 0)
        throw TSerialDeviceException("port not open");
}

void TSerialPort::WriteBytes(const uint8_t* buf, int count) {
    if (write(Fd, buf, count) < count)
        throw TSerialDeviceException("serial write failed");
    if (Dbg) {
        // TBD: move this to libwbmqtt (HexDump?)
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Write:" << std::hex << std::setfill('0');
        for (int i = 0; i < count; ++i)
            std::cerr << " " << std::setw(2) << int(buf[i]);
        std::cerr << std::endl;
        std::cerr.flags(f);
    }
}

bool TSerialPort::Select(const std::chrono::microseconds& us)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

#if 0
    // too verbose
    if (Dbg)
        std::cerr << "Select on " << Settings.Device << ": " << ms.count() << " us" << std::endl;
#endif

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);
    if (us.count() > 0) {
        tv.tv_sec = us.count() / 1000000;
        tv.tv_usec = us.count();
        tvp = &tv;
    }

    int r = select(Fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0)
        throw TSerialDeviceException("select() failed");

    return r > 0;
}

uint8_t TSerialPort::ReadByte()
{
    CheckPortOpen();

    if (!Select(Settings.ResponseTimeout))
        throw TSerialDeviceTransientErrorException("timeout");

    uint8_t b;
    if (read(Fd, &b, 1) < 1)
        throw TSerialDeviceException("read() failed");

    if (Dbg) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "Read: " << std::hex << std::setw(2) << std::setfill('0') << int(b) << std::endl;
        std::cerr.flags(f);
    }

    return b;
}

// Reading becomes unstable when using timeout less than default because of bufferization
int TSerialPort::ReadFrame(uint8_t* buf, int size,
                           const std::chrono::microseconds& timeout,
                           TFrameCompletePred frame_complete)
{
    CheckPortOpen();
    int nread = 0;
    while (nread < size) {
        if (frame_complete && frame_complete(buf, nread)) {
            // XXX A hack.
            // The problem is that if we don't pause here and the
            // serial client switches to another device after
            // processing this frame, that device may miss the frame
            // boundary and consider the last response (from this
            // device) and the query (from the master) to be single
            // frame. On the other hand, we don't want to use
            // device-specific frame timeout here as it can be quite
            // long. The proper solution would be perhaps ensuring
            // that there's a pause of at least
            // DeviceConfig->FrameTimeoutMs before polling each
            // device.
            usleep(DefaultFrameTimeout.count());
            break;
        }

        if (!Select(!nread ? Settings.ResponseTimeout :
                    timeout.count() < 0 ? DefaultFrameTimeout :
                    timeout))
            break; // end of the frame

        // We don't want to use non-blocking IO in general
        // (e.g. we want blocking writes), but we don't want
        // read() call below to block because actual frame
        // size is not known at this point. So we must
        // know how many bytes are available
        int nb;
        if (ioctl(Fd, FIONREAD, &nb) < 0)
            throw TSerialDeviceException("FIONREAD ioctl() failed");
        if (!nb)
            continue; // shouldn't happen, actually
        if (nb > size - nread)
            nb = size - nread;

        int n = read(Fd, buf + nread, nb);
        if (n < 0)
            throw TSerialDeviceException("read() failed");
        if (n < nb) // may happen only due to a kernel/driver bug
            throw TSerialDeviceException("short read()");

        nread += nb;
    }
    
    if (!nread)
        throw TSerialDeviceTransientErrorException("request timed out");

    if (Dbg) {
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
    while (Select(NoiseTimeout)) {
        if (read(Fd, &b, 1) < 1)
            throw TSerialDeviceException("read() failed");
        if (Dbg) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "read noise: " << std::hex << std::setfill('0') << std::setw(2) << int(b) << std::endl;
            std::cerr.flags(f);
        }

    }
}

void TSerialPort::Sleep(const std::chrono::microseconds& us)
{
    usleep(us.count());
}

TAbstractSerialPort::TTimePoint TSerialPort::CurrentTime() const
{
    return std::chrono::steady_clock::now();
}

bool TSerialPort::Wait(PBinarySemaphore semaphore, const TTimePoint& until)
{
    if (Dbg) {
        std::cerr << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() <<
            ": Wait until " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(until.time_since_epoch()).count() <<
            std::endl;
        }
            
    return semaphore->Wait(until);
}
