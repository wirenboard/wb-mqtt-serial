#pragma once

#include <variant>

#include "serial_port_settings.h"
#include "tcp_port_settings.h"

class TFeaturePort;

struct TFramedTcpPortSettings: TTcpPortSettings
{
    //! Modbus TCP framing instead of Modbus RTU, only a TCP port can do it
    bool ModbusTcp = false;
};

using TPortSettings = std::variant<TSerialPortSettings, TFramedTcpPortSettings>;

bool PortMatches(const TPortSettings& portSettings, const TFeaturePort& port);
