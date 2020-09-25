#pragma once

#include "port_settings.h"

#include <string>
#include <sstream>

struct TSerialPortSettings;

::std::ostream& operator<<(::std::ostream& os, const TSerialPortSettings& settings);

struct TSerialPortSettings final: TPortSettings
{
    TSerialPortSettings(std::string device = "/dev/ttyS0",
                        int baudRate = 9600,
                        char parity = 'N',
                        int dataBits = 8,
                        int stopBits = 1)
        : Device(device)
        , BaudRate(baudRate)
        , Parity(parity)
        , DataBits(dataBits)
        , StopBits(stopBits)
    {}

    std::string ToString() const override
    {
        std::ostringstream ss;
        ss << *this;
        return ss.str();
    }

    std::string Device;
    int         BaudRate;
    char        Parity;
    int         DataBits;
    int         StopBits;
};

using PSerialPortSettings = std::shared_ptr<TSerialPortSettings>;

inline ::std::ostream& operator<<(::std::ostream& os, const TSerialPortSettings& settings) {
    return os << "<" << settings.Device << " " << settings.BaudRate <<
        " " << settings.DataBits << " " << settings.Parity << settings.StopBits << ">";
}

