#include "tcp_port.h"
#include "serial_exc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <netinet/in.h>
#include <netdb.h>

#include <sys/types.h>
#include <sys/socket.h>

using namespace std;

TTcpPort::TTcpPort(const PTcpPortSettings & settings)
    : TFileDescriptorPort(settings)
    , Settings(settings)
{}

void TTcpPort::Open()
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
        ostringstream ss;
        ss << "connect error: " << error;
        throw TSerialDeviceException(ss.str());
    }
}

