#include "tcp_port.h"
#include "serial_exc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <iostream>

#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "log.h"

#define LOG(logger) ::logger.Log() << "[tcp port] "

using namespace std;

namespace {
    const int CONNECTION_TIMEOUT_S = 5;

    // Additional timeout for reading from tcp port. It is caused by intermediate hardware and internal Linux processing
    const std::chrono::microseconds TCPLag = std::chrono::microseconds(100000);
}

TTcpPort::TTcpPort(const PTcpPortSettings & settings)
    : Settings(settings)
    , RemainingFailCycles(settings->ConnectionMaxFailCycles)
{}

void TTcpPort::CycleBegin()
{
    if (!IsOpen()) {
        Open();
    }
}

void TTcpPort::Open()
{
    auto begin = chrono::steady_clock::now();

    try {
        OpenTcpPort();
        OnConnectionOk();
        LastInteraction = std::chrono::steady_clock::now();
    } catch (const TSerialDeviceException & e) {
        LOG(Error) << "port " << Settings->ToString() << ": " << e.what();
        Reset();

        // if failed too fast - sleep remaining time
        auto deltaUs = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - begin).count();
        auto connectionTimeoutUs = CONNECTION_TIMEOUT_S * 1000000;

        if (deltaUs < connectionTimeoutUs) {
            usleep(connectionTimeoutUs - deltaUs);
        }
    }
}

void TTcpPort::OpenTcpPort()
{
    if (Fd >= 0) {
        throw TSerialDeviceException("port already open");
    }

    Fd = socket(AF_INET, SOCK_STREAM, 0);

    if (Fd < 0) {
        auto error = errno;
        ostringstream ss;
        ss << "cannot open tcp port: " << error;
        throw TSerialDeviceException(ss.str());
    }

    // set socket to non-blocking state
    auto arg = fcntl(Fd, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(Fd, F_SETFL, arg);

    struct sockaddr_in serv_addr;
    struct hostent * server;

    server = gethostbyname(Settings->Address.c_str());

    if (!server) {
        ostringstream ss;
        ss << "no such host: " << Settings->Address;
        throw TSerialDeviceException(ss.str());
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

    serv_addr.sin_port = htons(Settings->Port);
    if (connect(Fd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
        auto error = errno;
        if (error == EINPROGRESS) {
            struct timeval tv;
            fd_set myset;

            tv.tv_sec = CONNECTION_TIMEOUT_S;
            tv.tv_usec = 0;

            FD_ZERO(&myset);
            FD_SET(Fd, &myset);

            auto res = select(Fd + 1, NULL, &myset, NULL, &tv);

            if (res > 0) {
                socklen_t lon = sizeof(int);
                int valopt;

                getsockopt(Fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
                if (valopt) {
                    ostringstream ss;
                    ss << "connect error: " << valopt << " - " << strerror(valopt);
                    throw TSerialDeviceException(ss.str());
                }
            } else if (res < 0 && errno != EINTR) {
                ostringstream ss;
                ss << "connect error: " << errno << " - " << strerror(errno);
                throw TSerialDeviceException(ss.str());
            } else {
                throw TSerialDeviceException("connect error: timeout");
            }
        } else {
            ostringstream ss;
            ss << "connect error: " << error << " - " << strerror(error);
            throw TSerialDeviceException(ss.str());
        }
    }

    // set socket back to blocking state
    arg = fcntl(Fd, F_GETFL, NULL);
    arg &= (~O_NONBLOCK);
    fcntl(Fd, F_SETFL, arg);
}

void TTcpPort::Reset() noexcept
{
    LOG(Warn) << Settings->ToString() <<  ": connection reset";
    try {
        Close();
    } catch (...) {
        // pass
    }
}

void TTcpPort::OnConnectionOk()
{
    LastSuccessfulCycle = std::chrono::steady_clock::now();
    RemainingFailCycles = Settings->ConnectionMaxFailCycles;
}

void TTcpPort::OnReadyEmptyFd()
{
    Close();
    throw TSerialDeviceTransientErrorException("socket closed");
}

void TTcpPort::WriteBytes(const uint8_t * buf, int count)
{
    if (IsOpen()) {
        Base::WriteBytes(buf, count);
    } else {
        LOG(Warn) << "attempt to write to not open port";
    }
}

uint8_t TTcpPort::ReadByte(const std::chrono::microseconds& timeout)
{
    return Base::ReadByte(timeout + TCPLag);
}

int TTcpPort::ReadFrame(uint8_t * buf, 
                        int count, 
                        const std::chrono::microseconds & responseTimeout,
                        const std::chrono::microseconds& frameTimeout,
                        TFrameCompletePred frame_complete)
{
    if (IsOpen()) {
        return Base::ReadFrame(buf, count, responseTimeout + TCPLag, frameTimeout + TCPLag, frame_complete);
    }
    LOG(Warn) << "Attempt to read from not open port";
    return 0;
}

void TTcpPort::CycleEnd(bool ok)
{
    // disable reconnect functionality option
    if (Settings->ConnectionTimeout.count() < 0 || Settings->ConnectionMaxFailCycles < 0) {
        return;
    }

    if (ok) {
        OnConnectionOk();
    } else {
        if (LastSuccessfulCycle == std::chrono::steady_clock::time_point()) {
            LastSuccessfulCycle = std::chrono::steady_clock::now();
        }

        if (RemainingFailCycles > 0) {
            --RemainingFailCycles;
        }

        if ((std::chrono::steady_clock::now() - LastSuccessfulCycle > Settings->ConnectionTimeout) &&
            RemainingFailCycles == 0)
        {
            Reset();
        }
    }
}
