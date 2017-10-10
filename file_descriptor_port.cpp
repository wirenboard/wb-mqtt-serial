#include "file_descriptor_port.h"
#include "serial_exc.h"
#include "binary_semaphore.h"

#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <sys/select.h>
#include <sys/ioctl.h>

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

    if (Debug()) {
        // TBD: move this to libwbmqtt (HexDump?)
        ios::fmtflags f(cerr.flags());
        cerr << "Write:" << hex << setfill('0');
        for (int i = 0; i < count; ++i)
            cerr << " " << setw(2) << int(buf[i]);
        cerr << endl;
        cerr.flags(f);
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

    if (Debug()) {
        ios::fmtflags f(cerr.flags());
        cerr << "Read: " << hex << setw(2) << setfill('0') << int(b) << endl;
        cerr.flags(f);
    }

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

    if (Debug()) {
        // TBD: move this to libwbmqtt (HexDump?)
        ios::fmtflags f(cerr.flags());
        cerr << "ReadFrame:" << hex << uppercase << setfill('0');
        for (int i = 0; i < nread; ++i) {
            cerr << " " << setw(2) << int(buf[i]);
        }
        cerr << endl;
        cerr.flags(f);
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
        if (Debug()) {
            ios::fmtflags f(cerr.flags());
            cerr << "read noise: " << hex << setfill('0') << setw(2) << int(b) << endl;
            cerr.flags(f);
        }
    }
}

void TFileDescriptorPort::Sleep(const chrono::microseconds & us)
{
    usleep(us.count());
}

bool TFileDescriptorPort::Wait(const PBinarySemaphore & semaphore, const TTimePoint & until)
{
    if (DebugEnabled) {
        cerr << chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count() <<
            ": Wait until " <<
            chrono::duration_cast<chrono::milliseconds>(until.time_since_epoch()).count() <<
            endl;
        }
            
    return semaphore->Wait(until);
}

void TFileDescriptorPort::SetDebug(bool debug)
{
    DebugEnabled = debug;
}

bool TFileDescriptorPort::Debug() const
{
    return DebugEnabled;
}

TTimePoint TFileDescriptorPort::CurrentTime() const
{
    return chrono::steady_clock::now();
}
