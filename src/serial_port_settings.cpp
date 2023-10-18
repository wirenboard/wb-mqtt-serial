#include "serial_port_settings.h"

#include <sstream>

TSerialPortConnectionSettings::TSerialPortConnectionSettings(int baudRate, char parity, int dataBits, int stopBits)
    : BaudRate(baudRate),
      Parity(parity),
      DataBits(dataBits),
      StopBits(stopBits)
{}

TSerialPortConnectionSettings::TSerialPortConnectionSettings(const TSerialPortConnectionSettings& other)
    : BaudRate(other.BaudRate),
      Parity(other.Parity),
      DataBits(other.DataBits),
      StopBits(other.StopBits)
{}

TSerialPortSettings::TSerialPortSettings(const std::string& device,
                                         const TSerialPortConnectionSettings& connectionSettings)
    : TSerialPortConnectionSettings(connectionSettings),
      Device(device)
{}

std::string TSerialPortSettings::ToString() const
{
    std::ostringstream ss;
    ss << "<" << Device << " " << ::ToString(*this) << ">";
    return ss.str();
}

void TSerialPortSettings::Set(const TSerialPortConnectionSettings& settings)
{
    *static_cast<TSerialPortConnectionSettings*>(this) = settings;
}

std::string ToString(const TSerialPortConnectionSettings& settings)
{
    std::ostringstream ss;
    ss << settings.BaudRate << " " << settings.DataBits << " " << settings.Parity << " " << settings.StopBits;
    return ss.str();
}
