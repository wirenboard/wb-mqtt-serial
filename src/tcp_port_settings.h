#pragma once

#include <sstream>
#include <string>

struct TTcpPortSettings
{
    TTcpPortSettings(const std::string& address = "localhost", uint16_t port = 0): Address(address), Port(port)
    {}

    std::string ToString() const
    {
        std::ostringstream ss;
        ss << "<" << Address << ":" << Port << ">";
        return ss.str();
    }

    std::string Address;
    uint16_t Port;
};
