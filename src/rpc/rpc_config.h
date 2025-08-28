#pragma once

#include <memory>

#include <wblib/json_utils.h>

#include "serial_port_settings.h"
#include "tcp_port_settings.h"

class TRPCConfig
{
public:
    void AddSerialPort(const TSerialPortSettings& settings);
    void AddTCPPort(const TTcpPortSettings& settings);
    void AddModbusTCPPort(const TTcpPortSettings& settings);
    Json::Value GetPortConfigs() const;

private:
    Json::Value PortConfigs{Json::arrayValue};
};

typedef std::shared_ptr<TRPCConfig> PRPCConfig;
