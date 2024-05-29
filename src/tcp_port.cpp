#include "tcp_port.h"
#include "serial_exc.h"

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "log.h"

#define LOG(logger) ::logger.Log() << "[tcp port] "

using namespace std;

namespace
{
    const int CONNECTION_TIMEOUT_S = 5;

    // Additional timeout for reading from tcp port. It is caused by intermediate hardware and internal Linux processing
    // Values are taken from old default timeouts
    const std::chrono::microseconds ResponseTCPLag = std::chrono::microseconds(500000);
    const std::chrono::microseconds FrameTCPLag = std::chrono::microseconds(150000);
}

TTcpPort::TTcpPort(const TTcpPortSettings& settings): Settings(settings)
{}

void TTcpPort::Open()
{
    if (IsOpen()) {
        throw TSerialDeviceException("port is already open");
    }

    struct hostent* server = gethostbyname(Settings.Address.c_str());
    if (!server) {
        throw TSerialDeviceException("no such host: " + Settings.Address);
    }

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        throw TSerialDeviceErrnoException("cannot open tcp port: ", errno);
    }

    struct sockaddr_in serv_addr;
    memset((char*)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memmove((char*)&serv_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
    serv_addr.sin_port = htons(Settings.Port);

    // set socket to non-blocking state
    auto arg = fcntl(Fd, F_GETFL, NULL);
    arg |= O_NONBLOCK;
    fcntl(Fd, F_SETFL, arg);

    try {
        if (connect(Fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            if (errno != EINPROGRESS) {
                throw std::runtime_error("connect error: " + FormatErrno(errno));
            }
            timeval tv = {CONNECTION_TIMEOUT_S, 0};
            fd_set myset;
            FD_ZERO(&myset);
            FD_SET(Fd, &myset);
            auto res = select(Fd + 1, NULL, &myset, NULL, &tv);
            if (res > 0) {
                socklen_t lon = sizeof(int);
                int valopt;
                getsockopt(Fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon);
                if (valopt) {
                    throw std::runtime_error("connect error: " + FormatErrno(valopt));
                }
            } else if (res < 0 && errno != EINTR) {
                throw std::runtime_error("connect error: " + FormatErrno(errno));
            } else {
                throw std::runtime_error("connect error: timeout");
            }
        }
    } catch (const std::runtime_error& e) {
        close(Fd);
        Fd = -1;
        throw TSerialDeviceException(GetDescription() + " " + e.what());
    }

    // set socket back to blocking state
    arg = fcntl(Fd, F_GETFL, NULL);
    arg &= (~O_NONBLOCK);
    fcntl(Fd, F_SETFL, arg);

    LastInteraction = std::chrono::steady_clock::now();
}

void TTcpPort::OnReadyEmptyFd()
{
    Close();
    throw TSerialDeviceTransientErrorException("socket closed");
}

void TTcpPort::WriteBytes(const uint8_t* buf, int count)
{
    if (IsOpen()) {
        Base::WriteBytes(buf, count);
    } else {
        LOG(Debug) << "Attempt to write to not open port";
    }
}

uint8_t TTcpPort::ReadByte(const std::chrono::microseconds& timeout)
{
    return Base::ReadByte(timeout + ResponseTCPLag);
}

TReadFrameResult TTcpPort::ReadFrame(uint8_t* buf,
                                     size_t count,
                                     const std::chrono::microseconds& responseTimeout,
                                     const std::chrono::microseconds& frameTimeout,
                                     TFrameCompletePred frame_complete)
{
    if (IsOpen()) {
        return Base::ReadFrame(buf,
                               count,
                               responseTimeout + ResponseTCPLag,
                               frameTimeout + FrameTCPLag,
                               frame_complete);
    }
    LOG(Debug) << "Attempt to read from not open port";
    return TReadFrameResult();
}

std::string TTcpPort::GetDescription(bool verbose) const
{
    if (verbose) {
        return Settings.ToString();
    }
    return Settings.Address + ":" + std::to_string(Settings.Port);
}
