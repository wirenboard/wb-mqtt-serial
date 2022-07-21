#include "rpc_config.h"
#include <algorithm>

void TRPCConfig::AddSerialPort(PPort Port, const std::string& Path)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&Port](PRPCPort RPCPort) { return Port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCSerialPort>(Port, Path);
        Ports.push_back(newRPCPort);
    }
}

void TRPCConfig::AddTCPPort(PPort Port, const std::string& Ip, uint16_t PortNumber)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&Port](PRPCPort RPCPort) { return Port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCTCPPort>(Port, Ip, PortNumber);
        Ports.push_back(newRPCPort);
    }
}

std::vector<PRPCPort> TRPCConfig::GetPorts()
{
    return Ports;
}