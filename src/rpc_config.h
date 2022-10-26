#pragma once
#include "rpc_port.h"
#include "serial_port_settings.h"
#include "tcp_port_settings.h"

class TRPCConfig
{
public:
    void AddSerialPort(PPort port, const TSerialPortSettings& settings);
    void AddTCPPort(PPort port, const TTcpPortSettings& settings);
    std::vector<PRPCPort> GetPorts();
    Json::Value GetPortConfigs() const;

private:
    std::vector<PRPCPort> Ports;
    Json::Value PortConfigs{Json::arrayValue};
};

typedef std::shared_ptr<TRPCConfig> PRPCConfig;
