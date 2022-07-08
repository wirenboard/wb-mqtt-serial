#include "rpc_config.h"
#include <algorithm>

void TRPCConfig::AddPort(PPort Port, enum TRPCPortMode Mode, std::string Path, std::string Ip, int PortNumber)
{
    auto existedPort = std::find_if(Ports.begin(), Ports.end(), [&Port](PRPCPort RPCPort) {
        if (Port == RPCPort->Port) {
            return true;
        }
        return false;
    });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCPort>();
        newRPCPort->Port = Port;
        newRPCPort->Mode = Mode;
        newRPCPort->Path = Path;
        newRPCPort->Ip = Ip;
        newRPCPort->PortNumber = PortNumber;
        Ports.push_back(newRPCPort);
    }
}

std::vector<PRPCPort> TRPCConfig::GetPorts()
{
    return Ports;
}