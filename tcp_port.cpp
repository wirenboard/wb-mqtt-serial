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

using namespace std;

namespace {
    const int CONNECTION_TIMEOUT_S = 5;
}

TTcpPort::TTcpPort(const PTcpPortSettings & settings)
    : TFileDescriptorPort(settings)
    , Settings(settings)
{}

void TTcpPort::CycleBegin()
{
    if (!IsOpen()) {
        Open();
    }
}

void TTcpPort::Open()
{
    try {
        OpenTcpPort();
    } catch (const TSerialDeviceException & e) {
        cerr << "ERROR at port " << Settings->ToString() << ": " << e.what() << endl;
        Reset();
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
    try {
        Close();
    } catch (...) {
        // pass
    }
}

bool TTcpPort::Select(const chrono::microseconds& us)
{
    bool selected = Base::Select(us);
    if (selected) {
        OnOkSelect();
    } else {
        OnBadSelect();
    }
    return selected;
}

void TTcpPort::WriteBytes(const uint8_t * buf, int count)
{
    if (IsOpen()) {
        Base::WriteBytes(buf, count);
    } else {
        cerr << "WARNING: attempt to write to not open port" << endl;
    }
}

int TTcpPort::ReadFrame(uint8_t * buf, int count, const std::chrono::microseconds & timeout, TFrameCompletePred frame_complete)
{
    if (IsOpen()) {
        return Base::ReadFrame(buf, count, timeout, frame_complete);
    } else {
        cerr << "WARNING: attempt to read from not open port" << endl;
    }
    return 0;
}

void TTcpPort::OnBadSelect()
{
    if (LastSuccessfulSelect == std::chrono::steady_clock::time_point()) {
		LastSuccessfulSelect = std::chrono::steady_clock::now();
	}

    if ((std::chrono::steady_clock::now() - LastSuccessfulSelect) > Settings->ConnectionTimeout) {
        Reset();
	}
}

void TTcpPort::OnOkSelect()
{
    LastSuccessfulSelect = std::chrono::steady_clock::now();
}
