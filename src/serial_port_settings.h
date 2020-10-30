#pragma once

#include <string>
#include <sstream>

struct TSerialPortSettings
{
    TSerialPortSettings(const std::string& device = "/dev/ttyS0",
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

    std::string ToString() const
    {
        std::ostringstream ss;
        ss << "<" << Device << " " << BaudRate << " " << DataBits << " " << Parity << StopBits << ">";
        return ss.str();
    }

    std::string Device;
    int         BaudRate;
    char        Parity;
    int         DataBits;
    int         StopBits;
};
