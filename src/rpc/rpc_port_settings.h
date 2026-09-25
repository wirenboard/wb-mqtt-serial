#pragma once

#include <string>
#include <variant>

#include <wblib/json_utils.h>

#include "port/serial_port_settings.h"
#include "port/tcp_port_settings.h"

struct TRPCTcpPortSettings: TTcpPortSettings
{
    //! Modbus TCP framing instead of Modbus RTU, only a TCP port can do it
    bool ModbusTcp = false;
};

using TRPCPortSettings = std::variant<TSerialPortSettings, TRPCTcpPortSettings>;

//! Port description in the same form as TPort::GetDescription(false) returns
std::string GetRPCPortDescription(const TRPCPortSettings& portSettings);

//! Connection settings of a serial port, a TCP port has none and gives the defaults
TSerialPortConnectionSettings GetRPCPortConnectionSettings(const TRPCPortSettings& portSettings);

/**
 * @brief Parses a port from an object with "path" or with "ip" or "address" and "port" fields.
 *
 * @throws TRPCException if the port is not defined
 */
TRPCPortSettings ParseRPCPort(
    const Json::Value& json,
    bool modbusTcp,
    const TSerialPortConnectionSettings& defaultSerialSettings = TSerialPortConnectionSettings());
