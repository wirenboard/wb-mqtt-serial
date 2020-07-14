#include "file_descriptor_port.h"
#include "serial_exc.h"
#include "binary_semaphore.h"
#include "log.h"

#include <wblib/utils.h>

#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <sys/select.h>
#include <sys/ioctl.h>

#define LOG(logger) ::logger.Log() << "[file descriptor port] "

using namespace std;

namespace {
    const chrono::milliseconds DefaultFrameTimeout(15);
    const chrono::milliseconds NoiseTimeout(10);
}

TFileDescriptorPort::TFileDescriptorPort(const PPortSettings & settings)
    : Fd(-1)
    , Settings(settings)
{}

TFileDescriptorPort::~TFileDescriptorPort()
{
    if (IsOpen()) {
        close(Fd);
    }
}

void TFileDescriptorPort::Close()
{
    CheckPortOpen();
    close(Fd);
    Fd = -1;
}

bool TFileDescriptorPort::IsOpen() const
{
    return Fd >= 0;
}

void TFileDescriptorPort::CheckPortOpen() const
{
    if (!IsOpen()) {
        throw TSerialDeviceException("port not open");
    }
}

void TFileDescriptorPort::WriteBytes(const uint8_t * buf, int count) {
    if (write(Fd, buf, count) < count) {
        throw TSerialDeviceException("serial write failed");
    }

    if (Debug.IsEnabled()) {
        std::stringstream output;
        output << "Write:" << hex << setfill('0');
        for (int i = 0; i < count; ++i)
            output << " " << setw(2) << int(buf[i]);
        LOG(Debug) << output.str();
    }
}

bool TFileDescriptorPort::Select(const chrono::microseconds& us)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

#if 0
    // too verbose
    if (Dbg)
        cerr << "Select on " << Settings.Device << ": " << ms.count() << " us" << endl;
#endif

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);
    if (us.count() > 0) {
        tv.tv_sec = us.count() / 1000000;
        tv.tv_usec = us.count();
        tvp = &tv;
    }

    int r = select(Fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0) {
        throw TSerialDeviceException("select() failed");
    }

    return r > 0;
}

uint8_t TFileDescriptorPort::ReadByte()
{
    CheckPortOpen();

    if (!Select(Settings->ResponseTimeout)) {
        throw TSerialDeviceTransientErrorException("timeout");
    }

    uint8_t b;
    if (read(Fd, &b, 1) < 1) {
        throw TSerialDeviceException("read() failed");
    }

    LOG(Debug) << "Read: " << hex << setw(2) << setfill('0') << int(b);

    return b;
}

// Reading becomes unstable when using timeout less than default because of bufferization
int TFileDescriptorPort::ReadFrame(uint8_t * buf, int size,
                           const chrono::microseconds& timeout,
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

        if (!Select(!nread ? Settings->ResponseTimeout :
                    timeout.count() < 0 ? DefaultFrameTimeout :
                    timeout))
            break; // end of the frame

        // We don't want to use non-blocking IO in general
        // (e.g. we want blocking writes), but we don't want
        // read() call below to block because actual frame
        // size is not known at this point. So we must
        // know how many bytes are available
        int nb;
        if (ioctl(Fd, FIONREAD, &nb) < 0) {
            throw TSerialDeviceException("FIONREAD ioctl() failed");
        }

        if (!nb) {
            continue; // shouldn't happen, actually
        }

        if (nb > size - nread) {
            nb = size - nread;
        }

        int n = read(Fd, buf + nread, nb);
        if (n < 0) {
            throw TSerialDeviceException("read() failed");
        }

        if (n < nb) { // may happen only due to a kernel/driver bug
            throw TSerialDeviceException("short read()");
        }

        nread += nb;
    }

    if (!nread) {
        throw TSerialDeviceTransientErrorException("request timed out");
    }

    if (Debug.IsEnabled()) {
        std::stringstream output;
        output << "ReadFrame:" << hex << setfill('0');
        for (int i = 0; i < nread; ++i)
            output << " " << setw(2) << int(buf[i]);
        LOG(Debug) << output.str();
    }

    return nread;
}

void TFileDescriptorPort::SkipNoise()
{
    uint8_t b;
    while (Select(NoiseTimeout)) {
        if (read(Fd, &b, 1) < 1) {
            throw TSerialDeviceException("read() failed");
        }
        LOG(Debug) << "read noise: " << hex << setfill('0') << setw(2) << int(b);
    }
}

void TFileDescriptorPort::Sleep(const chrono::microseconds & us)
{
    usleep(us.count());
}

bool TFileDescriptorPort::Wait(const PBinarySemaphore & semaphore, const TTimePoint & until)
{
    LOG(Debug) << chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count() <<
            ": Wait until " <<
            chrono::duration_cast<chrono::milliseconds>(until.time_since_epoch()).count();

    return semaphore->Wait(until);
}

TTimePoint TFileDescriptorPort::CurrentTime() const
{
    return chrono::steady_clock::now();
}
