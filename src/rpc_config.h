#pragma once
#include "rpc_port.h"

class TRPCConfig
{
public:
    void AddSerialPort(PPort Port, std::string Path);
    void AddTCPPort(PPort Port, std::string Ip, uint16_t PortNumber);
    std::vector<PRPCPort> GetPorts();

private:
    std::vector<PRPCPort> Ports;
};

typedef std::shared_ptr<TRPCConfig> PRPCConfig;