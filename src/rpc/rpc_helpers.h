#pragma once
#include "serial_driver.h"
#include <wblib/json_utils.h>

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);
PPort InitPort(const Json::Value& request);