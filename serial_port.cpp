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
#include <utility>


#include "serial_exc.h"
#include "serial_port.h"

namespace {
    const std::chrono::milliseconds DefaultFrameTimeout(15);
    const std::chrono::milliseconds NoiseTimeout(10);
};

TLibModbusContext::TLibModbusContext(const TSerialPortSettings& settings)
    : Inner(modbus_new_rtu(settings.Device.c_str(), settings.BaudRate,
                           settings.Parity, settings.DataBits, settings.StopBits))
{
    modbus_set_error_recovery(Inner, MODBUS_ERROR_RECOVERY_PROTOCOL); // FIXME

    if (settings.ResponseTimeout.count() > 0) {
        std::chrono::milliseconds response_timeout = settings.ResponseTimeout;
        struct timeval tv;
        tv.tv_sec = response_timeout.count() / 1000;
        tv.tv_usec = (response_timeout.count() % 1000) * 1000;
#if LIBMODBUS_VERSION_CHECK(3,1,2)
        modbus_set_response_timeout(Inner, tv.tv_sec, tv.tv_usec);
#else
        modbus_set_response_timeout(Inner, &tv);
#endif
    }
}

TAbstractSerialPort::~TAbstractSerialPort() {}

TSerialPort::TSerialPort(const TSerialPortSettings& settings)
    : Settings(settings),
      Context(new TLibModbusContext(settings)),
      Dbg(false),
      Fd(-1) {}

TSerialPort::~TSerialPort()
{
    if (Fd >= 0)
        modbus_close(Context->Inner);
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
    if (modbus_connect(Context->Inner) < 0)
        throw TSerialDeviceException("cannot open serial port");
    Fd = modbus_get_socket(Context->Inner);
}

void TSerialPort::Close()
{
    CheckPortOpen();
    modbus_close(Context->Inner);
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

PLibModbusContext TSerialPort::LibModbusContext() const
{
    return Context;
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
