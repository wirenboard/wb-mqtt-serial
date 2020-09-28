#pragma once

#include <string>
#include <sstream>

const int DEFAULT_PORT_FAIL_CYCLES = 2;

struct TTcpPortSettings
{
    TTcpPortSettings(const std::string& address = "localhost",
                     uint16_t port = 0,
                     const std::chrono::milliseconds& connectionTimeout = std::chrono::milliseconds(5000),
                     int failCycles = DEFAULT_PORT_FAIL_CYCLES)
        : Address(address)
        , Port(port)
        , ConnectionTimeout(connectionTimeout)
        , ConnectionMaxFailCycles(failCycles)
    {}

    std::string ToString() const
    {
        std::ostringstream ss;
        ss << "<" << Address << ":" << Port << "  " << ConnectionTimeout.count() << "ms, " << ConnectionMaxFailCycles <<">";
        return ss.str();
    }

    std::string                 Address;
    uint16_t                    Port;
    std::chrono::milliseconds   ConnectionTimeout;
    int                         ConnectionMaxFailCycles;
};
