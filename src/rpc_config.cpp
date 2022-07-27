#include "rpc_config.h"
#include <algorithm>

void TRPCConfig::AddSerialPort(PPort port, const std::string& path)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&port](PRPCPort RPCPort) { return port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCSerialPort>(port, path);
        Ports.push_back(newRPCPort);
    }
}

void TRPCConfig::AddTCPPort(PPort port, const std::string& ip, uint16_t portNumber)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&port](PRPCPort RPCPort) { return port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCTCPPort>(port, ip, portNumber);
        Ports.push_back(newRPCPort);
    }
}

std::vector<PRPCPort> TRPCConfig::GetPorts()
{
    return Ports;
}