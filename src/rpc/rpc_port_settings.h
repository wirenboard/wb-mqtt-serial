#pragma once

#include <string>

#include <wblib/json_utils.h>

#include "port/port_settings.h"

//! Port description in the same form as TPort::GetDescription(false) returns
std::string GetRPCPortDescription(const TPortSettings& portSettings);

//! Connection settings of a serial port, a TCP port has none and gives the defaults
TSerialPortConnectionSettings GetRPCPortConnectionSettings(const TPortSettings& portSettings);

/**
 * @brief Parses a port from an object with "path" or with "ip" or "address" and "port" fields.
 *
 * @throws TRPCException if the port is not defined
 */
TPortSettings ParseRPCPort(
    const Json::Value& json,
    bool modbusTcp,
    const TSerialPortConnectionSettings& defaultSerialSettings = TSerialPortConnectionSettings());
