#pragma once
#include "rpc_port.h"

class TRPCConfig
{
public:
    void AddPort(PPort Port, enum TRPCPortMode Mode, std::string Path, std::string Ip, int PortNumber);
    std::vector<PRPCPort> GetPorts();

private:
    std::vector<PRPCPort> Ports;
};

typedef std::shared_ptr<TRPCConfig> PRPCConfig;