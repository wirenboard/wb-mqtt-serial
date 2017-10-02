#pragma once

#include "port_settings.h"

#include <string>
#include <ostream>

struct TTcpPortSettings final: TPortSettings
{
    TTcpPortSettings(const std::string & address = "localhost",
                     uint16_t port = 0,
                     const std::chrono::milliseconds & responseTimeout = std::chrono::milliseconds(500))
        : TPortSettings(responseTimeout)
        , Address(address)
        , Port(port)
    {}
    
    std::string ToString() const override
    {
        return Address + ":" + std::to_string(Port);
    }

    std::string Address;
    uint16_t    Port;
};

using PTcpPortSettings = std::shared_ptr<TTcpPortSettings>;

inline ::std::ostream& operator<<(::std::ostream& os, const TTcpPortSettings & settings) {
    return os << "<" << settings.Address << ":" << settings.Port <<
        " timeout " << settings.ResponseTimeout.count() << ">";
}
