#pragma once

#include <string>

struct TSerialPortConnectionSettings
{
    TSerialPortConnectionSettings(int baudRate = 9600, char parity = 'N', int dataBits = 8, int stopBits = 1);
    TSerialPortConnectionSettings(const TSerialPortConnectionSettings& other);

    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
};

struct TSerialPortSettings: public TSerialPortConnectionSettings
{
    TSerialPortSettings(const std::string& device = "/dev/ttyS0",
                        const TSerialPortConnectionSettings& connectionSettings = TSerialPortConnectionSettings());

    void Set(const TSerialPortConnectionSettings& settings);

    std::string ToString() const;

    std::string Device;
};

std::string ToString(const TSerialPortConnectionSettings& settings);
