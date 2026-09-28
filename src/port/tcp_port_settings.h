#pragma once

#include <cstdint>
#include <string>

struct TTcpPortSettings
{
    TTcpPortSettings(const std::string& address = "localhost", uint16_t port = 0): Address(address), Port(port)
    {}

    std::string ToString() const
    {
        return "<" + GetDescription() + ">";
    }

    std::string GetDescription() const
    {
        return Address + ":" + std::to_string(Port);
    }

    std::string Address;
    uint16_t Port;
};
