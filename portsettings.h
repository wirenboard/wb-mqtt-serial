#pragma once

#include <string>
#include <ostream>

struct TSerialPortSettings
{
    TSerialPortSettings(std::string device = "/dev/ttyS0",
                        int baud_rate = 9600,
                        char parity = 'N',
                        int data_bits = 8,
                        int stop_bits = 1,
                        int response_timeout_ms = 500)
        : Device(device), BaudRate(baud_rate), Parity(parity),
          DataBits(data_bits), StopBits(stop_bits),
          ResponseTimeoutMs(response_timeout_ms) {}

    std::string Device;
    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
    int ResponseTimeoutMs;
};

inline ::std::ostream& operator<<(::std::ostream& os, const TSerialPortSettings& settings) {
    return os << "<" << settings.Device << " " << settings.BaudRate <<
        " " << settings.DataBits << " " << settings.Parity << settings.StopBits <<
        " timeout " << settings.ResponseTimeoutMs << ">";
}

