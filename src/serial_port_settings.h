#pragma once

#include <sstream>
#include <string>
#include <wblib/json/json.h>

struct TSerialPortByteFormat
{
    TSerialPortByteFormat(char parity, int dataBits, int stopBits)
        : Parity(parity),
          DataBits(dataBits),
          StopBits(stopBits)
    {}

    char Parity;
    int DataBits;
    int StopBits;
};

struct TSerialPortSettings: public TSerialPortByteFormat
{
    TSerialPortSettings(const std::string& device = "/dev/ttyS0",
                        int baudRate = 9600,
                        char parity = 'N',
                        int dataBits = 8,
                        int stopBits = 1)
        : TSerialPortByteFormat(parity, dataBits, stopBits),
          Device(device),
          BaudRate(baudRate)
    {}

    std::string ToString() const
    {
        std::ostringstream ss;
        ss << "<" << Device << " " << BaudRate << " " << DataBits << " " << Parity << " " << StopBits << ">";
        return ss.str();
    }

    Json::Value ToJson() const
    {
        Json::Value jsonValue;
        jsonValue["path"] = Device;
        jsonValue["baud_rate"] = BaudRate;
        jsonValue["data_bits"] = DataBits;
        jsonValue["parity"] = std::string(1, Parity);
        jsonValue["stop_bits"] = StopBits;
        return jsonValue;
    }

    std::string Device;
    int BaudRate;
};
