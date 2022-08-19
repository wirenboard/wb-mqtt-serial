#pragma once
#include "rpc_port.h"

class TRPCConfig
{
public:
    void AddSerialPort(PPort port, const std::string& path);
    void AddTCPPort(PPort port, const std::string& ip, uint16_t portNumber);
    std::vector<PRPCPort> GetPorts();

private:
    std::vector<PRPCPort> Ports;
};

typedef std::shared_ptr<TRPCConfig> PRPCConfig;