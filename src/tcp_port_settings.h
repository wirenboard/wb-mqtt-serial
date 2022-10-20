#pragma once

#include <sstream>
#include <string>
#include <wblib/json/json.h>

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

    Json::Value ToJson() const
    {
        Json::Value jsonValue;
        jsonValue["address"] = Address;
        jsonValue["port"] = Port;
        return jsonValue;
    }

    std::string Address;
    uint16_t Port;
};
