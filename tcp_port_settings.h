#pragma once

#include "port_settings.h"

#include <string>
#include <sstream>

struct TTcpPortSettings;

::std::ostream& operator<<(::std::ostream& os, const TTcpPortSettings & settings);

struct TTcpPortSettings final: TPortSettings
{
    TTcpPortSettings(const std::string & address = "localhost",
                     uint16_t port = 0,
                     const std::chrono::milliseconds & responseTimeout = std::chrono::milliseconds(500),
                     const std::chrono::milliseconds & connectionTimeout = std::chrono::milliseconds(5000))
        : TPortSettings(responseTimeout)
        , Address(address)
        , Port(port)
        , ConnectionTimeout(connectionTimeout)
    {}

    std::string ToString() const override
    {
        std::ostringstream ss;
        ss << *this;
        return ss.str();
    }

    std::string                 Address;
    uint16_t                    Port;
    std::chrono::milliseconds   ConnectionTimeout;
};

using PTcpPortSettings = std::shared_ptr<TTcpPortSettings>;

inline ::std::ostream& operator<<(::std::ostream& os, const TTcpPortSettings & settings) {
    return os << "<" << settings.Address << ":" << settings.Port <<
        " timeout " << settings.ResponseTimeout.count() << ">";
}
