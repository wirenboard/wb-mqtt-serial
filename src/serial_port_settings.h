#pragma once

#include <sstream>
#include <string>

struct TSerialPortConnectionSettings
{
    TSerialPortConnectionSettings(int baudRate = 9600, char parity = 'N', int dataBits = 8, int stopBits = 1)
        : BaudRate(baudRate),
          Parity(parity),
          DataBits(dataBits),
          StopBits(stopBits)
    {}

    TSerialPortConnectionSettings(const TSerialPortConnectionSettings& other)
        : BaudRate(other.BaudRate),
          Parity(other.Parity),
          DataBits(other.DataBits),
          StopBits(other.StopBits)
    {}

    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
};

struct TSerialPortSettings: public TSerialPortConnectionSettings
{
    TSerialPortSettings(const std::string& device = "/dev/ttyS0",
                        const TSerialPortConnectionSettings& connectionSettings = TSerialPortConnectionSettings())
        : TSerialPortConnectionSettings(connectionSettings),
          Device(device)
    {}

    void Set(const TSerialPortConnectionSettings& settings)
    {
        *static_cast<TSerialPortConnectionSettings*>(this) = settings;
    }

    std::string ToString() const
    {
        std::ostringstream ss;
        ss << "<" << Device << " " << BaudRate << " " << DataBits << " " << Parity << " " << StopBits << ">";
        return ss.str();
    }

    std::string Device;
};
