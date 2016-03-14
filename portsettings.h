#pragma once

#include <chrono>
#include <string>
#include <ostream>

struct TSerialPortSettings
{
    TSerialPortSettings(std::string device = "/dev/ttyS0",
                        int baud_rate = 9600,
                        char parity = 'N',
                        int data_bits = 8,
                        int stop_bits = 1,
                        const std::chrono::milliseconds& response_timeout = std::chrono::milliseconds::zero())
        : Device(device), BaudRate(baud_rate), Parity(parity),
          DataBits(data_bits), StopBits(stop_bits),
          ResponseTimeout(response_timeout) {}

    std::string Device;
    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
    std::chrono::milliseconds ResponseTimeout;
};

inline ::std::ostream& operator<<(::std::ostream& os, const TSerialPortSettings& settings) {
    return os << "<" << settings.Device << " " << settings.BaudRate <<
        " " << settings.DataBits << " " << settings.Parity << settings.StopBits <<
        " timeout " << settings.ResponseTimeout.count() << ">";
}

