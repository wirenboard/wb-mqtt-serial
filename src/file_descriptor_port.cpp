#include "file_descriptor_port.h"
#include "common_utils.h"
#include "serial_exc.h"

#include <iomanip>
#include <iostream>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include <wblib/utils.h>

#include "log.h"

using namespace std;

#define LOG(logger) ::logger.Log() << "[port] "

namespace
{
    const chrono::milliseconds NoiseTimeout(1);
    const chrono::milliseconds ContinuousNoiseTimeout(100);
    const int ContinuousNoiseReopenNumber = 3;
}

TFileDescriptorPort::TFileDescriptorPort(): Fd(-1)
{}

TFileDescriptorPort::~TFileDescriptorPort()
{
    if (TFileDescriptorPort::IsOpen()) {
        close(Fd);
    }
}

void TFileDescriptorPort::Close()
{
    if (IsOpen()) {
        close(Fd);
        Fd = -1;
    }
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

void TFileDescriptorPort::WriteBytes(const uint8_t* buf, int count)
{
    auto res = write(Fd, buf, count);
    if (res < count) {
        if (res < 0) {
            throw TSerialDeviceErrnoException("serial write failed: ", errno);
        }
        stringstream ss;
        ss << "serial write failed: " << res << " bytes of " << count << " is written";
        throw TSerialDeviceException(ss.str());
    }

    LastInteraction = std::chrono::steady_clock::now();

    if (::Debug.IsEnabled()) {
        LOG(Debug) << GetDescription(false) << ": Write: " << WBMQTT::HexDump(buf, count);
    }
}

bool TFileDescriptorPort::Select(const chrono::microseconds& us)
{
    fd_set rfds;
    struct timeval tv, *tvp = 0;

    FD_ZERO(&rfds);
    FD_SET(Fd, &rfds);
    if (us.count() > 0) {
        tv.tv_sec = us.count() / 1000000;
        tv.tv_usec = us.count() % 1000000;
        tvp = &tv;
    }

    int r = select(Fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0) {
        throw TSerialDeviceErrnoException("TFileDescriptorPort::Select() failed: ", errno);
    }

    return r > 0;
}

void TFileDescriptorPort::OnReadyEmptyFd()
{}

uint8_t TFileDescriptorPort::ReadByte(const chrono::microseconds& timeout)
{
    CheckPortOpen();

    if (!Select(timeout)) {
        throw TSerialDeviceTransientErrorException("timeout");
    }

    uint8_t b;
    if (read(Fd, &b, 1) < 1) {
        throw TSerialDeviceException("read() failed");
    }

    LastInteraction = std::chrono::steady_clock::now();

    LOG(Debug) << GetDescription(false) << ": Read: " << hex << setw(2) << setfill('0') << int(b);

    return b;
}

size_t TFileDescriptorPort::ReadAvailableData(uint8_t* buf, size_t max_read)
{
    // We don't want to use non-blocking IO in general
    // (e.g. we want blocking writes), but we don't want
    // read() call below to block because actual frame
    // size is not known at this point. So we must
    // know how many bytes are available
    int nb = 0;
    if (ioctl(Fd, FIONREAD, &nb) < 0) {
        throw TSerialDeviceException("FIONREAD ioctl() failed");
    }

    // Got Fd as ready for read from select, but no actual data to read
    if (!nb) {
        OnReadyEmptyFd();
        return 0;
    }

    if (nb >= 0 && static_cast<size_t>(nb) > max_read) {
        nb = max_read;
    }

    int n = read(Fd, buf, nb);
    if (n < 0) {
        throw TSerialDeviceException("read() failed");
    }

    if (n < nb) { // may happen only due to a kernel/driver bug
        throw TSerialDeviceException("short read()");
    }

    return nb;
}

// Reading becomes unstable when using timeout less than default because of bufferization
TReadFrameResult TFileDescriptorPort::ReadFrame(uint8_t* buf,
                                                size_t size,
                                                const std::chrono::microseconds& responseTimeout,
                                                const std::chrono::microseconds& frameTimeout,
                                                TFrameCompletePred frame_complete)
{
    CheckPortOpen();
    TReadFrameResult res;

    if (!size) {
        return res;
    }

    util::TSpentTimeMeter spentTime(std::chrono::steady_clock::now);
    spentTime.Start();

    // Will wait first byte up to responseTimeout us
    auto selectTimeout = responseTimeout;
    while (res.Count < size) {
        if (frame_complete && frame_complete(buf, res.Count)) {
            break;
        }

        if (!Select(selectTimeout))
            break; // end of the frame

        size_t nb = ReadAvailableData(buf + res.Count, size - res.Count);

        // Got Fd as ready for read from select, but no actual data to read
        if (nb == 0) {
            continue;
        }

        // Got something, switch to frameTimeout to detect frame boundary
        // Delay between bytes in one message can't be more than frameTimeout
        selectTimeout = frameTimeout;
        if (res.Count == 0) {
            res.ResponseTime = spentTime.GetSpentTime();
        }
        res.Count += nb;
    }

    if (!res.Count) {
        throw TResponseTimeoutException();
    }

    LastInteraction = std::chrono::steady_clock::now();

    if (::Debug.IsEnabled()) {
        LOG(Debug) << GetDescription(false) << ": ReadFrame: " << WBMQTT::HexDump(buf, res.Count);
    }

    return res;
}

void TFileDescriptorPort::SkipNoise()
{
    uint8_t buf[255] = {};
    auto start = std::chrono::steady_clock::now();
    int ntries = 0;

    while (Select(NoiseTimeout)) {
        size_t nread = ReadAvailableData(buf, sizeof(buf) / sizeof(buf[0]));
        auto diff = std::chrono::steady_clock::now() - start;

        if (::Debug.IsEnabled()) {
            LOG(Debug) << "read noise: " << WBMQTT::HexDump(buf, nread);
        }

        // if we are still getting data for already "ContinuousNoiseTimeout" milliseconds
        if (nread > 0) {
            LastInteraction = std::chrono::steady_clock::now();

            if (diff > ContinuousNoiseTimeout) {
                if (ntries < ContinuousNoiseReopenNumber) {
                    LOG(Debug) << "continuous unsolicited data flow detected, reopen the port";
                    Reopen();
                    ntries += 1;
                    start = std::chrono::steady_clock::now();
                } else {
                    throw TSerialDeviceTransientErrorException("continuous unsolicited data flow");
                }
            }
        }
    }
}

void TFileDescriptorPort::SleepSinceLastInteraction(const chrono::microseconds& us)
{
    auto now = chrono::steady_clock::now();
    auto delta = chrono::duration_cast<chrono::microseconds>(now - LastInteraction);
    std::this_thread::sleep_for(us - delta);
    LOG(Debug) << GetDescription(false) << ": Sleep " << us.count() << " us";
}
