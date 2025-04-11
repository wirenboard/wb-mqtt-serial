#pragma once
#include "serial_port_settings.h"
#include <wblib/json_utils.h>

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);
